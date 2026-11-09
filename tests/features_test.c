/* SPDX-License-Identifier: MIT */
/* tests/features_test.c - the comprehensive feature platform, measured.
 *
 * Split-screen is proven by PIXELS: each fake window fills its whole clip
 * with a different colour, and the gate asserts neither colour leaked into
 * the other half. Permissions, sandbox, adaptive freeze, theft lock,
 * dynamic palette, widget resize and the AI queue are all real state.
 *
 * Hardware we do not have is asserted as ABSENT, never faked: NFC, UWB and
 * an on-device model all report 0 / "host only".
 */

#include "apps/bootui/bootui.h"
#include "apps/settings/features.h"
#include "core/platform.h"
#include "gui/gfx/font_5x7.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/wallpaper.h"
#include "gui/widgets/widget_grid.h"
#include "gui/window_manager.h"
#include "hal/include/wifi.h"
#include "hal/include/bluetooth.h"
#include "hal/include/usb_gadget.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W 1080
#define H 2340
#define STRIDE (W * 2)
/* Virtual y of Features row i in group 0: one group header, then 34px rows. */
#define ROW_V(i) (58 + 22 + (i) * 34 + 14)

static uint16_t fb[H][W];
static int g_checks, g_fail;
static enum wifi_status g_wifi = WIFI_STATUS_OFF;
static enum bt_status g_bt = BT_STATUS_OFF;
static int g_cable;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) printf("ok    %s\n", label);
    else { g_fail++; printf("FAIL  %s\n", label); }
}

/* ---- host seams ------------------------------------------------------------ */
void compositor_to(int app_id) { (void)app_id; }
enum wifi_status wifi_status(void) { return g_wifi; }
enum bt_status bt_status(void) { return g_bt; }
int usb_gadget_cable(void) { return g_cable; }

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
#define C_RED  rgb565(0xE0, 0x30, 0x30)
#define C_BLUE rgb565(0x20, 0x60, 0xE0)

/* Two fake windows: each paints the WHOLE screen. Only the gfx clip keeps
 * them in their own pane. */
static void draw_red(struct wm_window *w) {
    struct umos_rect r = {0, 0, (uint16_t)gfx_logical_width(),
                          (uint16_t)gfx_logical_height()};
    (void)w;
    gfx_fill_rect(&r, (struct umos_color){0xE0, 0x30, 0x30, 0xFF});
}
static void draw_blue(struct wm_window *w) {
    struct umos_rect r = {0, 0, (uint16_t)gfx_logical_width(),
                          (uint16_t)gfx_logical_height()};
    (void)w;
    gfx_fill_rect(&r, (struct umos_color){0x20, 0x60, 0xE0, 0xFF});
}

static struct wm_window w_red = { .name = "red",   .draw = draw_red };
static struct wm_window w_blue = { .name = "blue", .draw = draw_blue };
static struct wm_window w_third = { .name = "third", .draw = draw_red };

static int count(int x0, int y0, int x1, int y1, uint16_t col) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) if (fb[y][x] == col) n++;
    return n;
}

static int dump(const char *p) {
    FILE *f = fopen(p, "wb");
    size_t n;
    if (!f) return 0;
    n = fwrite(&fb[0][0], 1, (size_t)W * H * 2, f);
    fclose(f);
    return n == (size_t)W * H * 2;
}

static void clear(void) {
    memset(fb, 0, sizeof(fb));
    gfx_init(&fb[0][0], W, H, STRIDE);
    gfx_clip_reset();
    theme_init();
    font_5x7_init();
}

int main(void) {
    char buf[128];
    int id;

    printf("== VQEAF feature platform, real state + real pixels ==\n");
    clear();
    platform_apps_init();
    widget_grid_init();
    features_init();
    wm_init();

    /* ---- UI & customization ------------------------------------------------ */
    printf("\n-- UI & customization --\n");
    check(platform_app_count() == 8, "8 system apps registered");
    check(platform_launcher() == 0, "the built-in Home is the default launcher");
    {
        int tp = platform_register_third_party_launcher("Nova");
        check(tp > 0 && platform_set_launcher(tp) == 0 &&
              platform_launcher() == tp,
              "a third-party launcher can be selected from the registry");
        check(platform_set_launcher(5) == -1,
              "a non-launcher app cannot become the launcher");
        check(platform_set_launcher(0) == 0, "switching back to Home works");
    }
    check(theme_mode() == THEME_DARK && g_theme.bg.r == 0 &&
          g_theme.bg.g == 0 && g_theme.bg.b == 0,
          "dark mode uses a true-black OLED background");
    theme_set_mode(THEME_LIGHT);
    check(g_theme.bg.r > 200 && theme_mode() == THEME_LIGHT,
          "light mode lifts the background");
    theme_set_mode(THEME_DARK);
    {
        uint16_t before = theme_accent565();
        int n = theme_apply_wallpaper();
        check(n > 0, "the wallpaper really yields sampled colours");
        check(theme_dynamic() == 1 && theme_accent565() != before,
              "Material You derives a new accent from those pixels");
        {
            uint16_t samples[12];
            int got = wallpaper_sample_palette(samples, 12);
            int found = 0;
            for (int i = 0; i < got; i++)
                if (samples[i] == theme_accent565()) found = 1;
            check(found, "the accent is one of the wallpaper's real pixels");
        }
    }
    theme_set_icon_pack(2);
    check(strcmp(theme_icon_pack_name(theme_icon_pack()), "Glyph") == 0,
          "icon pack switching is real state");
    theme_set_font_scale(9);
    check(theme_font_scale() == THEME_FONT_SCALE_MAX,
          "font scale is clamped, not trusted");
    theme_set_font_scale(1);

    /* ---- resizable widgets -------------------------------------------------- */
    printf("\n-- resizable widgets --\n");
    id = widget_grid_add(WG_CLOCK, 0, 0, 2, 1);
    check(id > 0 && widget_grid_count() == 1, "a 2x1 widget is placed");
    check(widget_grid_add(WG_BATTERY, 1, 0, 2, 1) == -1,
          "an overlapping widget is refused");
    check(widget_grid_add(WG_BATTERY, 0, 0, 5, 1) == -1,
          "an out-of-grid widget is refused");
    {
        int x, y, w, h;
        widget_grid_rect(id, &x, &y, &w, &h);
        check(w == 2 * WG_CELL - WG_GAP, "2 cells produce the expected width");
        check(widget_grid_hit(x + 4, y + 4) == id, "hit test finds the widget");
    }
    check(widget_grid_resize(id, 3, 1) == 0, "resize 2x1 -> 3x1 succeeds");
    check(widget_grid_resize(id, 5, 1) == -1,
          "resize past the grid is refused");
    {
        int b = widget_grid_add(WG_NOTES, 0, 1, 1, 1);
        check(b > 0, "a second widget fits on the row below");
        check(widget_grid_resize(id, 3, 2) == -1,
              "resizing into the second widget is refused");
        check(widget_grid_remove(b) == 0, "the second widget can be removed");
    }
    check(widget_grid_add(WG_NOTES, 2, 0, 2, 1) == -1,
          "a widget cannot overlap the resized 3x1 one");
    check(widget_grid_move(id, 0, 2) == 0, "widgets can be moved");
    check(widget_grid_remove(id) == 0 && widget_grid_count() == 0,
          "removal frees the cells");

    /* ---- permissions + sandbox --------------------------------------------- */
    printf("\n-- security --\n");
    check(platform_grant(4, PERM_CAMERA, PERM_ONE_TIME) == 0,
          "one-time camera grant accepted");
    check(platform_check(4, PERM_CAMERA, 1) == 1,
          "first foreground access is allowed");
    check(platform_perm_mode(4, PERM_CAMERA) == PERM_DENY,
          "the one-time grant is consumed");
    check(platform_check(4, PERM_CAMERA, 1) == 0,
          "second access is denied");
    check(platform_grant(5, PERM_LOCATION, PERM_WHILE_IN_USE) == 0 &&
          platform_check(5, PERM_LOCATION, 1) == 1 &&
          platform_check(5, PERM_LOCATION, 0) == 0,
          "while-in-use blocks background collection");
    {
        int before = platform_alerts();
        platform_grant(6, PERM_MIC, PERM_ALWAYS);
        platform_check(6, PERM_MIC, 0);
        check(platform_alerts() == before + 1,
              "background mic access raises a privacy alert");
        platform_clear_alerts();
        check(platform_alerts() == 0, "alerts can be acknowledged");
    }
    check(platform_sandbox_check(4, "/data/app/4/notes.txt") == 1,
          "an app reads its own namespace");
    check(platform_sandbox_check(4, "/data/app/5/secret.txt") == 0,
          "cross-app data is refused");
    check(platform_sandbox_check(4, "/data/app/4/../5/x") == 0,
          "path traversal is refused");
    check(platform_sandbox_denials() == 2, "both refusals were counted");

    /* ---- adaptive battery / RAM (rule engine, NOT ML) ---------------------- */
    printf("\n-- adaptive --\n");
    check(platform_adaptive_is_ml() == 0,
          "the policy is honestly labelled a rule engine, not ML");
    platform_record_usage(4, 0, 5);        /* rarely foreground, wakes often */
    platform_record_usage(1, 30000, 0);    /* actively used */
    platform_adaptive_tick(120, 1, 0);
    check(platform_frozen(4) == 1, "a stale wake-hungry app is frozen");
    check(platform_frozen(1) == 0, "an actively used app is kept awake");
    check(platform_frozen_count() == 1, "freeze counter is exact");
    platform_unfreeze(4);
    platform_adaptive_tick(121, 0, 1);
    check(platform_frozen(4) == 0,
          "a healthy/charging battery does not freeze apps");

    /* ---- theft protection --------------------------------------------------- */
    printf("\n-- theft protection --\n");
    platform_feed_motion(0, 0, 1000);
    check(platform_theft_tick() == 0 && !platform_locked(),
          "normal gravity does not lock the phone");
    platform_feed_motion(1800, -900, 600);
    check(platform_theft_tick() == 1 && platform_locked(),
          "a snatch-scale acceleration spike locks the screen");
    platform_unlock();
    check(!platform_locked(), "the owner can unlock");

    /* ---- connectivity + AI seam -------------------------------------------- */
    printf("\n-- connectivity & AI --\n");
    check(platform_nfc_available() == 0 && platform_uwb_available() == 0,
          "NFC/UWB honestly report NO hardware on violet");
    g_wifi = WIFI_STATUS_CONNECTED;
    g_bt = BT_STATUS_ON;
    g_cable = 1;
    check((platform_connectivity() & (CONN_WIFI | CONN_BT | CONN_USB)) ==
              (CONN_WIFI | CONN_BT | CONN_USB),
          "connectivity reports the three radios that are actually up");
    check((platform_connectivity() & (CONN_NFC | CONN_UWB)) == 0,
          "the NFC/UWB bits are never set");
    check(platform_ai_available() == 0,
          "no on-device model is claimed");
    check(platform_ai_enqueue("summarise battery") > 0 &&
          platform_ai_pending() == 1,
          "an AI intent is queued for the host");
    check(platform_ai_pull(buf, sizeof(buf)) > 0 &&
          strcmp(buf, "summarise battery") == 0 &&
          platform_ai_pending() == 0,
          "the host can pull the exact intent back");

    /* ---- split screen: real clipped pixels --------------------------------- */
    printf("\n-- split screen & floating windows --\n");
    wm_register_window(UMOS_APP_MESSAGES, &w_red);
    wm_register_window(UMOS_APP_BROWSER, &w_blue);
    wm_register_window(UMOS_APP_GALLERY, &w_third);
    check(wm_set_split(UMOS_APP_MESSAGES, UMOS_APP_BROWSER, 128) == 0,
          "split screen accepts two registered apps");
    {
        int lx, ly, lw, lh, rx, ry, rw, rh;
        wm_split_rects(&lx, &ly, &lw, &lh, &rx, &ry, &rw, &rh);
        check(lw == W / 2 && rw == W / 2 && rx == W / 2,
              "50/50 panes tile the whole logical width exactly");
        check(wm_split_hit(10, 10) == 0 && wm_split_hit(W - 10, 10) == 1,
              "touch maps to the correct pane");
        clear();
        check(wm_frame_multitask() == 2, "both panes were drawn");
        check(count(0, 0, W / 2, 100, C_RED) == (W / 2) * 100,
              "the left pane is fully red");
        check(count(W / 2, 0, W, 100, C_BLUE) == (W / 2) * 100,
              "the right pane is fully blue");
        check(count(W / 2, 0, W, 100, C_RED) == 0 &&
              count(0, 0, W / 2, 100, C_BLUE) == 0,
              "NEITHER pane leaked into the other (clip works)");
        check(dump("out/features_split.raw"), "wrote split-screen RGB565 frame");
    }
    check(wm_set_split(UMOS_APP_MESSAGES, UMOS_APP_MESSAGES, 128) == -1 &&
          wm_set_split(UMOS_APP_MESSAGES, UMOS_APP_BROWSER, 250) == -1,
          "same-app split and an out-of-range ratio are refused");
    wm_split_end();

    check(wm_float_add(UMOS_APP_GALLERY, 40, 100, 200, 160) == 0 &&
          wm_float_count() == 1, "a floating window opens");
    check(wm_float_add(UMOS_APP_MESSAGES, 300, 400, 200, 160) == 0,
          "a second floating window opens");
    check(wm_float_top_at(310, 410) == UMOS_APP_MESSAGES,
          "the newest window is on top");
    check(wm_float_raise(UMOS_APP_GALLERY) == 0 &&
          wm_float_top_at(50, 110) == UMOS_APP_GALLERY,
          "raising changes z-order");
    check(wm_float_move(UMOS_APP_GALLERY, -50, -50) == 0,
          "dragging clamps to the surface instead of losing the window");
    {
        int aid, x, y, w, h;
        wm_float_rect(wm_float_count() - 1, &aid, &x, &y, &w, &h);
        check(x >= 0 && y >= 0, "the clamped position stays on screen");
    }
    check(wm_float_remove(UMOS_APP_GALLERY) == 0 &&
          wm_float_count() == 1, "removing one floating window keeps the other");

    /* ---- features page: real badges, real taps ----------------------------- */
    printf("\n-- Features page --\n");
    clear();
    features_open();
    features_draw();
    check(dump("out/features_page.raw"), "wrote the Features page frame");
    check(count(0, 150, W, 900, 0) < (W * 750) / 2,
          "the page is inked, not a blank frame");
    /* Tap "Dark theme" row (group 0 header 22px, row index 2). */
    {
        int vx = 100, vy = ROW_V(2);
        int k = bui_scale();
        enum theme_mode before = theme_mode();
        features_event(1, (uint32_t)(vx * k), (uint32_t)(vy * k));
        check(theme_mode() != before, "tapping the Dark row toggles the mode");
    }
    features_event(2, 0, 0);
    check(!features_visible(), "BACK closes the Features page");

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
