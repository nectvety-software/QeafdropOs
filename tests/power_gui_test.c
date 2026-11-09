/* SPDX-License-Identifier: MIT */
/* tests/power_gui_test.c - host gate for the charge line on the lock screen.
 *
 * The line is drawn from bsp/violet/power_supply.c reading a fixture
 * power_supply tree through core/sysio.c - the same code path the phone walks
 * over /sys. Frames measured, with their in-run negative controls:
 *   - no fixture at all      -> battery_read -1, the screen claims nothing
 *   - plugged out (usb 0)    -> no charge line even though the battery answers
 *   - Charging 84%           -> glyph + "Charging 84%" in primary/text inks
 *   - Charging 40%           -> strictly less glyph fill: the bar tracks capacity
 *
 * Build (from repo root): make power-check
 */

#include "apps/bootui/bootui.h"
#include "apps/lock/lock_screen.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "gui/window_manager.h"
#include "hal/include/battery.h"
#include "hal/include/hal.h"

#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define W 1080
#define H 2340
#define STRIDE (W * 2)

static uint16_t fb[H][W];

static int g_checks = 0;
static int g_fail = 0;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) {
        printf("ok    %s\n", label);
    } else {
        g_fail++;
        printf("FAIL  %s\n", label);
    }
}

/* ---- stubs for the pieces the phone supplies ------------------------------- */

static struct wm_window *g_win;

void wm_register_window(int app_id, struct wm_window *w) {
    (void)app_id;
    g_win = w;
}

void wm_open(int app_id) { (void)app_id; }
void wm_close(int app_id) { (void)app_id; }

struct hal_touch *hal_touch(void) { return NULL; }

void umos_logf(const char *fmt, ...) {
    va_list ap;
    (void)fmt;
    va_start(ap, fmt);
    va_end(ap);
}

/* ---- measured facts -------------------------------------------------------- */

static uint16_t at_v(int vx, int vy) { return fb[vy * 3][vx * 3]; }

static uint16_t c565(struct umos_color c) {
    return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

static int count_color(int vx0, int vy0, int vx1, int vy1, uint16_t col) {
    int n = 0;
    for (int vy = vy0; vy < vy1; vy++)
        for (int vx = vx0; vx < vx1; vx++)
            if (at_v(vx, vy) == col) n++;
    return n;
}

static uint64_t hash_fb(void) {
    uint64_t h = 1469598103934665603ULL;
    const uint16_t *p = &fb[0][0];
    for (size_t i = 0; i < (size_t)W * H; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int dump(const char *path) {
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) return 0;
    n = fwrite(&fb[0][0], 1, (size_t)W * H * 2, f);
    fclose(f);
    return n == (size_t)W * H * 2;
}

/* ---- fixture power_supply tree --------------------------------------------- */

#define FX "out/power_fx/sys"

static void mkdir_p(const char *path) {
    char buf[256];
    size_t i;
    for (i = 0; path[i] && i + 1 < sizeof(buf); i++) {
        buf[i] = path[i];
        buf[i + 1] = '\0';
        if (path[i] == '/' && i > 0) _mkdir(buf);
    }
    _mkdir(buf);
}

static void put_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("FAIL  cannot create fixture %s\n", path); g_fail++; return; }
    fputs(text, f);
    fclose(f);
}

static void fx_write(const char *supply, const char *attr, const char *val) {
    char path[256];
    snprintf(path, sizeof(path), "%s/class/power_supply/%s", FX, supply);
    mkdir_p(path);
    snprintf(path, sizeof(path), "%s/class/power_supply/%s/%s", FX, supply, attr);
    put_file(path, val);
}

static void build_fixture(const char *status, const char *capacity,
                          const char *usb_online) {
    fx_write("battery", "status", status);
    fx_write("battery", "capacity", capacity);
    fx_write("usb", "online", usb_online);
}

/* the charge line occupies this virtual band (lock_screen.c y0 = 706) */
#define LINE_X0 60
#define LINE_X1 300
#define LINE_Y0 700
#define LINE_Y1 726

/* The phone hands the lock sheet a backdrop - the compositor paints the
 * wallpaper and the screen lays a translucent plate over it. Filling the panel
 * with the theme background first is the host's stand-in for that backdrop,
 * so every pixel this gate counts is the sheet's own ink, not an empty buffer. */
static void paint(void) {
    struct umos_rect full = {0, 0, (uint16_t)W, (uint16_t)H};
    gfx_fill_rect(&full, g_theme.bg);
    g_win->draw(g_win);
}

/* lock_draw samples the seam on the first frame and every 16th after it, so a
 * control has to paint a full cycle the way the phone's loop does. */
static void paint_sampled(void) {
    for (int i = 0; i < 17; i++) paint();
}

int main(void) {
    struct battery_state bs;
    uint64_t h_unplugged, h_charging;
    int fill_84, fill_40;

    printf("== charge line: real power_supply seam, 1080x2340, K=3 ==\n");
    gfx_init(&fb[0][0], W, H, STRIDE);
    theme_init();
    lock_init();
    check(g_win != NULL, "the lock window registered with its draw hook");

    /* control 1: no board answers the seam */
    battery_set_sysfs_root("out/power_fx/missing/sys");
    check(battery_read(&bs) == -1, "no fixture: the seam refuses to answer");
    paint();
    check(lock_charge_visible() == 0, "nothing is claimed without an answer");
    check(count_color(LINE_X0, LINE_Y0, LINE_X1, LINE_Y1, c565(g_theme.primary)) == 0,
          "no primary ink on a screen the battery never spoke to");

    /* control 2: the battery answers, but no charger is plugged */
    build_fixture("Full\n", "100\n", "0\n");
    battery_set_sysfs_root(FX);
    check(battery_read(&bs) == 0 && bs.capacity == 100 && !bs.charging &&
          !bs.plugged, "fixture reads back Full/100/unplugged through sysio");
    paint_sampled();
    check(lock_charge_visible() == 0, "an unplugged battery draws no charge line");
    h_unplugged = hash_fb();
    check(dump("out/lock_unplugged.raw"), "wrote unplugged RGB565 dump");

    /* the feature: charger in, battery charging */
    build_fixture("Charging\n", "84\n", "1\n");
    check(battery_read(&bs) == 0 && bs.capacity == 84 && bs.charging &&
          bs.plugged, "the same seam now says Charging at 84 percent");
    paint_sampled(); /* first paint after 16 idle frames samples again */
    check(lock_charge_visible() == 1, "plugged: the seam says charge and the screen agrees");
    fill_84 = count_color(LINE_X0, LINE_Y0, LINE_X1, LINE_Y1, c565(g_theme.primary));
    check(fill_84 > 40, "the glyph and the line put real primary ink on the glass");
    check(count_color(LINE_X0, LINE_Y0, LINE_X1, LINE_Y1, c565(g_theme.text)) > 60,
          "'Charging 84%' is rasterised in the text ink");
    h_charging = hash_fb();
    check(h_unplugged != h_charging, "plugging the charger repaints the screen");
    check(dump("out/lock_charging.raw"), "wrote charging RGB565 dump");

    /* the fill must track the battery, not sit at a remembered level */
    build_fixture("Charging\n", "40\n", "1\n");
    paint_sampled();
    fill_40 = count_color(LINE_X0, LINE_Y0, LINE_X1, LINE_Y1, c565(g_theme.primary));
    check(fill_40 < fill_84, "a lower capacity draws a shorter fill, not the old one");

    /* unplugging again erases the line from the glass */
    build_fixture("Not charging\n", "40\n", "0\n");
    paint_sampled();
    check(lock_charge_visible() == 0, "unplug removes the line with the charger");
    check(count_color(LINE_X0, LINE_Y0, LINE_X1, LINE_Y1, c565(g_theme.primary)) == 0,
          "no primary ink survives the unplug");

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
