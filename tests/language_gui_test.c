/* SPDX-License-Identifier: MIT */
/* tests/language_gui_test.c - the Language sheet on real framebuffers.
 *
 * Draws the sheet the phone draws, then taps the rows through bui_map_touch
 * exactly as a finger would: portrait 1080x2340 (K=3) and landscape 2340x1080
 * (K=3 on the 780x360 canvas). It checks the selection really changed the
 * strings, that the active row carries accent ink, and that every row stays
 * inside its canvas. The clock is checked for the HH:MM contract on the same
 * run (host has no RTC, so it must fall back without inventing a wall time).
 */

#include "apps/settings/language.h"
#include "apps/bootui/bootui.h"
#include "core/clock.h"
#include "core/i18n.h"
#include "gui/gfx/gfx.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PW 1080
#define PH 2340
#define LW 2340
#define LH 1080

static uint16_t fb[LH][LW];
static int checks;
static int fails;

static void check(int cond, const char *label) {
    checks++;
    if (cond) printf("ok    %s\n", label);
    else { fails++; printf("FAIL  %s\n", label); }
}

static uint16_t c565(struct umos_color c) {
    return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

static int count_color(int x0, int y0, int x1, int y1, uint16_t col) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (fb[y][x] == col) n++;
    return n;
}

/* Tap the centre of row i. The canvas is K=3 in both orientations and the
 * view origin is zero for this sheet, so virtual * 3 is the physical pixel. */
static int tap_row(int i) {
    int x, y, w, h;
    if (!language_row_rect(i, &x, &y, &w, &h)) return 0;
    return language_event(BUI_EV_TOUCH, (uint32_t)((x + w / 2) * 3),
                          (uint32_t)((y + (h - 4) / 2) * 3));
}

int main(void) {
    char clock_text[8];
    int x, y, w, h;

    printf("== Language sheet: portrait 1080x2340, K=3 ==\n");
    gfx_init(&fb[0][0], PW, PH, PW * 2);
    language_init();
    language_open();
    check(language_visible() == 1, "the sheet opens");
    check(language_row_count() == 8, "seven languages plus automatic");

    clock_hhmm(clock_text, (int)sizeof(clock_text));
    check(strlen(clock_text) == 5 && clock_text[2] == ':' &&
          clock_text[0] >= '0' && clock_text[0] <= '9' &&
          clock_text[3] >= '0' && clock_text[3] <= '9',
          "the clock answers HH:MM");
    check(clock_is_wall() == 0,
          "the host has no RTC, so the clock reports uptime rather than a wall time");

    language_draw();
    check(fb[0][0] != 0, "the sheet painted the panel");

    for (int i = 0; i < 8; i++) {
        check(language_row_rect(i, &x, &y, &w, &h) &&
              x >= 0 && y >= 0 && x + w <= 360 && y + h <= 780,
              "row stays inside the portrait canvas");
    }
    {
        int x1, y1, w1, h1, x2, y2, w2, h2;
        language_row_rect(3, &x1, &y1, &w1, &h1);
        language_row_rect(4, &x2, &y2, &w2, &h2);
        check(y2 >= y1 + h1, "portrait rows do not overlap");
    }

    /* The shipped default is Vietnamese; the check mark is ink on that row. */
    check(i18n_selected() == I18N_VI, "Vietnamese is the default");
    language_row_rect(I18N_VI, &x, &y, &w, &h);
    check(count_color(x, y, x + w, y + h, c565(BUI_TEAL)) > 40,
          "the active row carries accent ink (chip + check)");

    check(tap_row(I18N_ES) == 1 && i18n_selected() == I18N_ES,
          "a tap selects Spanish");
    check(strcmp(i18n_t(I18N_LANGUAGE), "Idioma") == 0,
          "the title immediately speaks Spanish");
    check(strcmp(i18n_t(I18N_SETTINGS), "Ajustes") == 0,
          "the settings label follows the language");

    check(tap_row(I18N_DE) == 1 && strcmp(i18n_t(I18N_LANGUAGE), "Sprache") == 0,
          "German is selectable and translated");
    check(tap_row(I18N_VI) == 1 && strcmp(i18n_t(I18N_LANGUAGE), "Ngon ngu") == 0,
          "Vietnamese is selectable and translated");

    check(tap_row(I18N_AUTO) == 1 && i18n_selected() == I18N_AUTO,
          "Automatic is a real selection");
    check(i18n_effective() == I18N_EN &&
          strcmp(i18n_t(I18N_LANGUAGE), "Language") == 0,
          "Automatic resolves to English on a board with no locale service");

    check(tap_row(I18N_JA) == 1 && i18n_selected() == I18N_JA &&
          i18n_effective() == I18N_EN,
          "Japanese keeps the preference while the ASCII face falls back to English");
    check(tap_row(I18N_EN) == 1 && i18n_selected() == I18N_EN,
          "English is selectable and translated");

    printf("\n== Language sheet: landscape 2340x1080, 780x360 ==\n");
    gfx_init(&fb[0][0], LW, LH, LW * 2);
    language_draw();
    {
        int x1, y1, w1, h1, x2, y2, w2, h2;
        check(language_row_rect(0, &x1, &y1, &w1, &h1) &&
              x1 >= 0 && y1 >= 0 && x1 + w1 <= 780 && y1 + h1 <= 360,
              "the first row fits the wide canvas");
        check(language_row_rect(7, &x2, &y2, &w2, &h2) &&
              x2 >= 0 && y2 >= 0 && x2 + w2 <= 780 && y2 + h2 <= 360,
              "the automatic row fits the wide canvas");
        check(x2 > x1 && y2 == 56 + 3 * 70,
              "landscape reflows to two columns of four rows");
    }
    check(tap_row(I18N_ZH) == 1 && i18n_selected() == I18N_ZH,
          "a landscape tap selects Chinese");
    check(i18n_effective() == I18N_EN,
          "Chinese falls back to English until a CJK face ships");
    check(count_color(0, 0, 780, 360, c565(BUI_TEAL)) > 40,
          "landscape rows really painted accent chips");

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
