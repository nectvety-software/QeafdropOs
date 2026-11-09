/* SPDX-License-Identifier: MIT */
/* tests/recovery_gui_test.c - host render gate for the two boot-mode screens.
 *
 * Binds a real 1080x2340 RGB565 framebuffer, calls the very same draw()
 * functions the phone calls, and asserts measured pixel facts: the procedural
 * V mark, ringed logo, split-colour wordmark, waves, the selected pill/row,
 * dashed Tips box, navigation, intent queue and confirmation defaults.
 *
 * Build (from repo root, zig cc native):
 *   zig cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx -Igui/widgets -Iapps \
 *       tests/recovery_gui_test.c apps/bootui/bootui.c \
 *       apps/fastboot/fastboot.c apps/recovery/recovery.c \
 *       gui/gfx/gfx.c gui/gfx/theme.c gui/gfx/font.c gui/gfx/font_5x7.c \
 *       -o out/recovery_gui_test.exe
 *   ./out/recovery_gui_test.exe out/bootui_fastboot.raw \
 *       out/bootui_recovery.raw out/bootui_confirm.raw
 */

#include "apps/bootui/bootui.h"
#include "apps/fastboot/fastboot.h"
#include "apps/recovery/recovery.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/font_5x7.h"
#include "gui/window_manager.h"
#include "hal/include/hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 1080
#define H 2340
#define STRIDE (W * 2)

static uint16_t fb[H][W];
static uint16_t small[780][360];

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

static struct wm_window *g_wins[UMOS_MAX_WINDOWS];
static int g_nwin;

void wm_register_window(int app_id, struct wm_window *w) {
    (void)app_id;
    if (g_nwin < UMOS_MAX_WINDOWS) g_wins[g_nwin++] = w;
}

void wm_open(int app_id) {
    for (int i = 0; i < g_nwin; i++)
        g_wins[i]->visible = (g_wins[i]->id == app_id);
}

void wm_close(int app_id) {
    for (int i = 0; i < g_nwin; i++)
        if (g_wins[i]->id == app_id) g_wins[i]->visible = 0;
}

struct hal_touch *hal_touch(void) { return NULL; }

/* ---- measured facts -------------------------------------------------------- */

static uint16_t at_v(int vx, int vy) { return fb[vy * 3][vx * 3]; }

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* Palette-derived: the assertions still demand the exact ink the screens are
 * specified to use, but read that ink from the one shared palette instead of
 * duplicating its numbers here. */
#define C_BG     rgb565(BUI_BG.r, BUI_BG.g, BUI_BG.b)
#define C_TEAL   rgb565(BUI_TEAL.r, BUI_TEAL.g, BUI_TEAL.b)
#define C_CYAN   rgb565(BUI_CYAN.r, BUI_CYAN.g, BUI_CYAN.b)
#define C_TEXT   rgb565(BUI_TEXT.r, BUI_TEXT.g, BUI_TEXT.b)
#define C_BORDER rgb565(BUI_BORDER.r, BUI_BORDER.g, BUI_BORDER.b)
#define C_W1     rgb565(BUI_WAVE_1.r, BUI_WAVE_1.g, BUI_WAVE_1.b)
#define C_W2     rgb565(BUI_WAVE_2.r, BUI_WAVE_2.g, BUI_WAVE_2.b)
#define C_W3     rgb565(BUI_WAVE_3.r, BUI_WAVE_3.g, BUI_WAVE_3.b)
#define C_CREST  rgb565(BUI_TEAL_DIM.r, BUI_TEAL_DIM.g, BUI_TEAL_DIM.b)

static int distinct_colors(void) {
    static unsigned char seen[65536];
    memset(seen, 0, sizeof(seen));
    int n = 0;
    for (int y = 0; y < H; y += 3)
        for (int x = 0; x < W; x += 3)
            if (!seen[fb[y][x]]) { seen[fb[y][x]] = 1; n++; }
    return n;
}

static int distinct_v(int vx0, int vy0, int vx1, int vy1, int step) {
    static unsigned char seen[65536];
    memset(seen, 0, sizeof(seen));
    int n = 0;
    for (int vy = vy0; vy < vy1; vy += step)
        for (int vx = vx0; vx < vx1; vx += step) {
            uint16_t v = at_v(vx, vy);
            if (!seen[v]) { seen[v] = 1; n++; }
        }
    return n;
}

static int count_color(int vx0, int vy0, int vx1, int vy1, uint16_t col) {
    int n = 0;
    for (int vy = vy0; vy < vy1; vy++)
        for (int vx = vx0; vx < vx1; vx++)
            if (at_v(vx, vy) == col) n++;
    return n;
}

/* Counts cool accent ink (blue-dominant, not red, not white) instead of one
 * hard-coded teal: the V mark is a gradient between the accent and its tint,
 * so the check follows the palette rather than duplicating it. */
static int accent_pixels(int vx0, int vy0, int vx1, int vy1) {
    int n = 0;
    for (int vy = vy0; vy < vy1; vy += 2)
        for (int vx = vx0; vx < vx1; vx += 2) {
            uint16_t v = at_v(vx, vy);
            int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            if (b > 12 && b > r && g >= r) n++;
        }
    return n;
}

/* The wave footer is three dark ribbons plus thin neon crests. Counting exact
 * palette colours is a more honest check than "many bright teal pixels", and
 * it stays true when the footer text covers part of the band. */
static int wave_colors(int vy0, int vy1) {
    const uint16_t palette[5] = { C_W1, C_W2, C_W3, C_CREST, C_CYAN };
    int n = 0;
    for (int i = 0; i < 5; i++)
        if (count_color(0, vy0, BUI_V_W, vy1, palette[i]) > 20) n++;
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
    if (!f) return 0;
    size_t n = fwrite(&fb[0][0], 1, (size_t)W * H * 2, f);
    fclose(f);
    return n == (size_t)W * H * 2;
}

/* ------------------------------------------------------------- fastboot UI */

static void check_fastboot_pixels(void) {
    printf("\n== Fastboot Mode: measured pixels ==\n");
    check(distinct_colors() >= 20, "frame carries a real palette (>=20 colours)");
    check(distinct_v(90, 88, 270, 213, 3) >= 8,
          "big V mark is a gradient, not a flat fill");
    check(accent_pixels(90, 88, 270, 213) > 1500,
          "V mark carries accent ink");
    check(count_color(60, 230, 300, 270, C_TEXT) > 400,
          "VQEAF wordmark is inked white");
    check(count_color(60, 230, 300, 270, C_CYAN) > 100,
          "OS wordmark is inked cyan (split-colour wordmark)");
    check(at_v(180, 440) == C_TEAL,
          "Continue pill has the teal selected border");
    check(at_v(300, 505) == C_BG,
          "unselected fastboot rows stay unboxed (no border drawn)");
    check(at_v(16, 320) == C_BORDER,
          "device info card has its border");
    check(wave_colors(715, 778) >= 3,
          "wave footer draws >=3 distinct ribbon / crest colours");
    check(count_color(40, 680, 320, 725, C_TEAL) > 100,
          "volume/power hint is inked in teal");
}

static void check_fastboot_behaviour(void) {
    printf("\n== Fastboot Mode: navigation + intents ==\n");
    check(fastboot_action_count() == 6, "6 fastboot menu entries");
    check(fastboot_selected() == 0, "initial selection = Continue");
    fastboot_event(BUI_EV_DOWN, 0, 0);
    check(fastboot_selected() == 1, "DOWN moves to Recovery Mode");
    fastboot_event(BUI_EV_UP, 0, 0);
    check(fastboot_selected() == 0, "UP moves back");
    for (int i = 0; i < 6; i++) fastboot_event(BUI_EV_DOWN, 0, 0);
    check(fastboot_selected() == 0, "selection wraps through 6 entries");

    fastboot_event(BUI_EV_DOWN, 0, 0);           /* Recovery Mode */
    fastboot_event(BUI_EV_ACTIVATE, 0, 0);
    check(fastboot_take_action() == FASTBOOT_ACT_RECOVERY,
          "Recovery Mode queues FASTBOOT_ACT_RECOVERY");
    check(fastboot_take_action() == FASTBOOT_ACT_NONE,
          "intent queue drains once");

    /* Flash Recovery writes the p77 partition: it MUST ask for consent, with
     * CANCEL selected by default, and queue nothing until CONFIRM is chosen. */
    fastboot_event(BUI_EV_TOUCH, 100 * 3, 570 * 3);   /* row 3: 562..594 */
    check(fastboot_confirming() == 1,
          "Flash Recovery opens the consent dialog");
    check(fastboot_take_action() == FASTBOOT_ACT_NONE,
          "no flash intent is queued before consent");
    check(strstr(fastboot_action_name(FASTBOOT_ACT_FLASH_RECOVERY), "p77") != NULL,
          "the consent dialog names the p77 partition");
    fastboot_event(BUI_EV_ACTIVATE, 0, 0);            /* default = CANCEL */
    check(fastboot_take_action() == FASTBOOT_ACT_NONE,
          "default CANCEL refuses the partition write");
    fastboot_event(BUI_EV_TOUCH, 100 * 3, 570 * 3);
    fastboot_event(BUI_EV_DOWN, 0, 0);                /* move to CONFIRM */
    fastboot_event(BUI_EV_ACTIVATE, 0, 0);
    check(fastboot_take_action() == FASTBOOT_ACT_FLASH_RECOVERY,
          "explicit CONFIRM queues FLASH_RECOVERY (an intent, not a write)");

    /* Factory Reset is destructive: it must open a confirm with CANCEL set. */
    fastboot_event(BUI_EV_TOUCH, 100 * 3, 610 * 3);
    check(fastboot_confirming() == 1, "Factory Reset opens confirmation");
    check(fastboot_take_action() == FASTBOOT_ACT_NONE,
          "no intent is queued before confirmation");
    fastboot_event(BUI_EV_ACTIVATE, 0, 0);       /* default = CANCEL */
    check(fastboot_take_action() == FASTBOOT_ACT_NONE,
          "default CANCEL writes nothing");
    fastboot_event(BUI_EV_TOUCH, 100 * 3, 610 * 3);
    fastboot_event(BUI_EV_DOWN, 0, 0);           /* move to CONFIRM */
    fastboot_event(BUI_EV_ACTIVATE, 0, 0);
    check(fastboot_take_action() == FASTBOOT_ACT_FACTORY_RESET,
          "explicit CONFIRM queues FACTORY_RESET");
}

/* ------------------------------------------------------------- recovery UI */

static void check_recovery_pixels(void) {
    printf("\n== Recovery Mode: measured pixels ==\n");
    check(distinct_colors() >= 20, "frame carries a real palette (>=20 colours)");
    check(at_v(180, 62) == C_TEAL,
          "outer ring is drawn at the top of the logo circle");
    check(distinct_v(125, 112, 235, 188, 3) >= 8,
          "V mark inside the rings is a gradient");
    check(count_color(60, 248, 300, 280, C_TEXT) > 300,
          "VQEAF OS wordmark inked white");
    check(count_color(60, 248, 300, 280, C_CYAN) > 80,
          "OS wordmark / Recovery subtitle inked cyan");
    check(at_v(180, 318) == C_TEAL,
          "selected row 0 has the teal border");
    check(at_v(180, 370) == C_BORDER,
          "unselected row 1 has the dim border");
    check(count_color(16, 318, 344, 624, C_TEXT) > 600,
          "six menu labels are rasterised");
    check(at_v(16, 636) == C_BORDER && at_v(21, 636) == C_BG,
          "Tips box uses a dashed border (5 on / 3 off)");
    check(wave_colors(715, 778) >= 3,
          "wave footer draws >=3 distinct ribbon / crest colours");
}

static void check_recovery_behaviour(void) {
    printf("\n== Recovery Mode: navigation + intents ==\n");
    check(recovery_action_count() == 6, "6 recovery menu entries");
    check(recovery_selected() == 0, "initial selection = Reboot System");
    recovery_event(RECOVERY_EV_DOWN, 0, 0);
    check(recovery_selected() == 1, "DOWN moves to Apply Update");
    recovery_event(RECOVERY_EV_UP, 0, 0);
    check(recovery_selected() == 0, "UP moves back");
    for (int i = 0; i < 6; i++) recovery_event(RECOVERY_EV_DOWN, 0, 0);
    check(recovery_selected() == 0, "selection wraps through 6 entries");

    recovery_event(RECOVERY_EV_ACTIVATE, 0, 0);
    check(recovery_take_action() == RECOVERY_ACT_REBOOT,
          "Reboot System queues RECOVERY_ACT_REBOOT");

    uint64_t before = hash_fb();
    recovery_set_status("Executor reports back");
    recovery_set_progress(50);
    recovery_draw();
    uint64_t mid = hash_fb();
    recovery_set_progress(-1);
    recovery_draw();
    uint64_t after = hash_fb();
    check(before != mid, "status + 50% progress repaint changes the frame");
    check(mid != after, "hiding the progress bar repaints again");

    recovery_event(RECOVERY_EV_TOUCH, 100 * 3, 370 * 3);  /* Apply Update */
    check(recovery_confirming() == 1, "Apply Update opens confirmation");
    check(recovery_take_action() == RECOVERY_ACT_NONE,
          "no FLASH intent before confirmation");
    recovery_draw();                     /* the overlay is a separate frame */
    uint64_t dialog = hash_fb();
    check(dialog != after, "confirm overlay changes the rendered glass");
    recovery_event(RECOVERY_EV_CANCEL, 0, 0);
    check(recovery_take_action() == RECOVERY_ACT_NONE,
          "CANCEL discards the destructive intent");
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv) {
    const char *fb_raw    = argc > 1 ? argv[1] : "out/bootui_fastboot.raw";
    const char *rc_raw    = argc > 2 ? argv[2] : "out/bootui_recovery.raw";
    const char *dlg_raw   = argc > 3 ? argv[3] : "out/bootui_confirm.raw";

    printf("== boot UI: 1080x2340 framebuffer, K=3 ==\n");
    gfx_init(&fb[0][0], W, H, STRIDE);
    theme_init();
    font_5x7_init();
    fastboot_init();
    recovery_init();
    fastboot_open();

    fastboot_draw();
    check_fastboot_pixels();
    check(dump(fb_raw), "wrote fastboot RGB565 dump");
    check_fastboot_behaviour();

    /* A clean recovery frame, not the one left over from the confirm tests. */
    fastboot_close();
    recovery_open();
    recovery_draw();
    check_recovery_pixels();
    check(dump(rc_raw), "wrote recovery RGB565 dump");
    check_recovery_behaviour();

    /* Re-open the dialog by touch, draw it, and capture it. */
    recovery_event(RECOVERY_EV_TOUCH, 100 * 3, 370 * 3);
    recovery_draw();
    check(recovery_confirming() == 1, "confirm overlay is open for capture");
    check(dump(dlg_raw), "wrote confirm dialog RGB565 dump");
    recovery_event(RECOVERY_EV_CANCEL, 0, 0);

    /* The same layout must survive K=1 on a 360x780 framebuffer. */
    gfx_init(&small[0][0], 360, 780, 360 * 2);
    recovery_draw();
    check(small[100][100] != 0, "recovery renders on a 360x780 framebuffer");
    fastboot_open();
    fastboot_draw();
    check(small[150][180] != 0, "fastboot renders on a 360x780 framebuffer");
    gfx_init(&fb[0][0], W, H, STRIDE);

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
