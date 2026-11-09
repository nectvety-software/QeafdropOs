/* SPDX-License-Identifier: MIT */
/* tests/keyboard_gui_test.c - Blueprint keyboard on a real RGB565 panel.
 *
 * Measures the poster's distinguishable facts (light grid sheet, dark plate,
 * silver keys with 1..0 superscripts, two orange keys) and the behaviour that
 * makes it a keyboard (typing, shift, ?123, backspace, emoji, rotation
 * intents). Rotated frames are captured too: the same draw() must land on a
 * swapped logical surface and the inverse touch map must still find keys.
 */

#include "apps/bootui/bootui.h"
#include "apps/keyboard/keyboard.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/font_5x7.h"
#include "gui/gfx/theme.h"
#include "hal/include/display.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W 1080
#define H 2340
#define STRIDE (W * 2)

static uint16_t fb[H][W];
static int g_checks, g_fail;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) printf("ok    %s\n", label);
    else { g_fail++; printf("FAIL  %s\n", label); }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
#define C_ORANGE rgb565(0xE8, 0x7A, 0x22)
#define C_NAVY   rgb565(0x22, 0x31, 0x4F)
#define C_PLATE  rgb565(0x0B, 0x0F, 0x15)
#define C_PAPER  rgb565(0xF2, 0xF6, 0xFC)

static uint16_t at(int x, int y) { return fb[y][x]; }

static int count_color(int x0, int y0, int x1, int y1, uint16_t col) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (at(x, y) == col) n++;
    return n;
}

static int dump(const char *path) {
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) return 0;
    n = fwrite(&fb[0][0], 1, (size_t)W * H * 2, f);
    fclose(f);
    return n == (size_t)W * H * 2;
}

static void clear(void) {
    memset(fb, 0, sizeof(fb));
    gfx_init(&fb[0][0], W, H, STRIDE);
    theme_init();
    font_5x7_init();
}

/* virtual -> physical device pixels at K=3 */
#define V(x) ((x) * 3)

static void tap(int vx, int vy) {
    keyboard_event(KEYBOARD_EV_TOUCH, (uint32_t)V(vx), (uint32_t)V(vy));
}

int main(void) {
    int arg = -99;
    int ox, oy;

    printf("== Blueprint keyboard, 1080x2340 RGB565, K=3 ==\n");
    clear();
    display_set_auto_rotate(0);
    display_set_orientation(DISPLAY_PORTRAIT);
    keyboard_init();
    check(keyboard_key_count() == 34,
          "four rows hold 10 + 9 + 9 + 6 = 34 keys");
    keyboard_open();
    check(keyboard_visible(), "the keyboard opens");
    keyboard_draw();
    keyboard_origin(&ox, &oy);
    check(ox == 0 && oy == 0, "portrait design is not offset");

    /* Poster palette, sampled from the real framebuffer. */
    check(count_color(V(0), V(0), V(360), V(120), C_PAPER) > 200000,
          "the light paper dominates the header area (not inverted)");
    check(count_color(V(16), V(126), V(344), V(372), C_PLATE) > 5000,
          "the near-black rounded plate dominates its box");
    check(count_color(V(24), V(310), V(344), V(362), C_ORANGE) > 4000,
          "orange ink covers the long spacebar and backspace");
    check(count_color(V(24), V(400), V(344), V(430), C_NAVY) > 30,
          "typed text is inked in dark navy below the plate");
    check(dump("out/keyboard_portrait.raw"), "wrote portrait RGB565 frame");

    /* Row 1 superscript: a small digit in the top-right of each light key. */
    check(count_color(V(27), V(136), V(54), V(152), C_NAVY) == 0,
          "the first row's superscript is dim, not navy");

    /* Typing. Row 1 key 0 = 'q' at x 27..54, y 136..188. */
    tap(40, 160);
    check(strcmp(keyboard_text(), "q") == 0, "tapping Q types a lowercase q");
    tap(40, 160);
    check(strcmp(keyboard_text(), "qq") == 0, "a second tap appends");
    tap(310, 280);                       /* orange backspace: x 290..336 */
    check(strcmp(keyboard_text(), "q") == 0, "the orange backspace deletes");
    tap(40, 270);                        /* dark Shift: x 24..64, y 252..304 */
    check(keyboard_shift() == 1, "Shift toggles");
    tap(40, 160);
    check(strcmp(keyboard_text(), "qQ") == 0, "Shift produces an uppercase Q");
    tap(40, 270);                        /* Shift off */
    tap(40, 340);                        /* ?123: x 24..64, y 310..362 */
    check(keyboard_symbols() == 1, "?123 switches to the symbol layer");
    tap(40, 160);
    check(strcmp(keyboard_text(), "qQ1") == 0,
          "the symbol layer types a digit");
    tap(40, 340);                        /* back to ABC */
    tap(118, 340);                       /* emoji key: x 102..134 */
    check(strstr(keyboard_text(), ":)") != NULL,
          "the emoji key inserts a smiley");
    tap(190, 340);                       /* orange spacebar: x 138..258 */
    check(keyboard_text()[strlen(keyboard_text()) - 1] == ' ',
          "the orange spacebar inserts a space");

    /* Rotation controls queue intents; the GUI never applies them itself. */
    check(keyboard_take_action(&arg) == KEYBOARD_ACT_NONE,
          "typing queues no rotation intent");
    tap(100, 650);                       /* "90" button: x 84..144, y 636..670 */
    check(keyboard_take_action(&arg) == KEYBOARD_ACT_ROTATE && arg == 1,
          "the 90 button queues a landscape intent");
    check(gfx_rotation() == GFX_ROT_0,
          "the keyboard did NOT rotate the panel by itself");
    tap(300, 650);                       /* AUTO: x 288..344 */
    check(keyboard_take_action(&arg) == KEYBOARD_ACT_ROTATE && arg == -2,
          "the AUTO button queues auto-rotate");

    check(keyboard_event(KEYBOARD_EV_BACK, 0, 0) == 1 &&
          !keyboard_visible(), "BACK closes the keyboard");

    /* ---- rotation transform: exact pixel + inverse touch ------------------ */
    printf("\n== gfx rotation transform ==\n");
    {
        int32_t lx, ly;
        /* For each angle, plot one logical pixel and read it back at the
         * physical spot the transform promises. */
        struct { enum gfx_rotation r; int x, y, px, py; } cases[] = {
            { GFX_ROT_0,   10, 20,   10,   20 },
            { GFX_ROT_90,  10, 20,   20, 2329 },
            { GFX_ROT_180, 10, 20, 1069, 2319 },
            { GFX_ROT_270, 10, 20, 1059,   10 },
        };
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            clear();
            gfx_set_rotation(cases[i].r);
            gfx_plot565(cases[i].x, cases[i].y, 0xF81F);
            check(at(cases[i].px, cases[i].py) == 0xF81F,
                  "one logical pixel lands at the promised physical pixel");
            gfx_map_touch(cases[i].px, cases[i].py, &lx, &ly);
            check(lx == cases[i].x && ly == cases[i].y,
                  "the inverse touch map returns the original logical point");
        }
        gfx_set_rotation(GFX_ROT_0);
    }

    /* ---- same keyboard drawn in landscape --------------------------------- */
    clear();
    display_set_orientation(DISPLAY_LANDSCAPE);
    check(gfx_rotation() == GFX_ROT_90 &&
          gfx_logical_width() == H && gfx_logical_height() == W,
          "landscape swaps the logical surface to 2340x1080");
    keyboard_open();
    keyboard_draw();
    keyboard_origin(&ox, &oy);
    check(ox > 0 && oy > 0, "the 360x780 design is centred on the wide surface");
    check(bui_vwidth() >= 360 && bui_vheight() >= 780,
          "the virtual canvas still fits the rotated surface");
    check(dump("out/keyboard_landscape.raw"), "wrote landscape RGB565 frame");

    /* A physical tap must still find the centred plate's first key. */
    {
        int32_t lx, ly;
        int vx, vy;
        /* virtual (40,160) -> logical (40,160) at K=1 + origin */
        lx = 40 + ox;
        ly = 160 + oy;
        /* logical -> physical, then feed that to bui_map_touch */
        gfx_map_touch(0, 0, &vx, &vy); /* just to keep types honest */
        (void)vx; (void)vy;
        {
            int32_t px = ly, py = (int32_t)H - 1 - lx;
            bui_map_touch((int)px, (int)py, &vx, &vy);
            check(vx == 40 + ox && vy == 160 + oy,
                  "a rotated physical tap maps back to the centred key");
        }
    }
    display_set_orientation(DISPLAY_PORTRAIT);
    keyboard_close();

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
