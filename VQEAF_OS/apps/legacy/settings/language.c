/* SPDX-License-Identifier: MIT */
/* apps/settings/language.c - Language sheet, 360x780 portrait / 780x360 wide.
 *
 * Drawn entirely with the bootui primitives so the phone's K=3 glass and the
 * host gates share one layout. The rows are computed by language_row_rect(),
 * the same function the hit test uses, so draw and input cannot drift apart.
 */

#include "apps/settings/language.h"

#include "apps/bootui/bootui.h"
#include "core/i18n.h"
#include "gui/gfx/gfx.h"

#define LANG_ROWS I18N_LANG_COUNT

static int g_vis;

static int land(void) { return bui_landscape(); }

static void row_geom(int i, int *x, int *y, int *w, int *h) {
    if (land()) {
        int col = i / 4;
        int row = i % 4;
        *x = 8 + col * 384;
        *y = 56 + row * 70;
        *w = 376;
        *h = 64;
    } else {
        *x = 8;
        *y = 72 + i * 76;
        *w = 344;
        *h = 70;
    }
}

int language_row_rect(int index, int *x, int *y, int *w, int *h) {
    if (index < 0 || index >= LANG_ROWS) return 0;
    row_geom(index, x, y, w, h);
    return 1;
}

int language_row_count(void) { return LANG_ROWS; }

void language_init(void) { g_vis = 0; }
void language_open(void) { g_vis = 1; }
void language_close(void) { g_vis = 0; }
int language_visible(void) { return g_vis; }

static void draw_header(void) {
    if (land()) {
        bui_text("LANGUAGE", 20, 8, 1, BUI_TEAL);
        bui_text(i18n_t(I18N_LANGUAGE), 20, 20, 2, BUI_TEXT);
        bui_hline(0, 46, bui_vwidth(), BUI_BORDER);
    } else {
        bui_text("LANGUAGE", 20, 14, 1, BUI_TEAL);
        bui_text(i18n_t(I18N_LANGUAGE), 20, 30, 3, BUI_TEXT);
        bui_hline(0, 58, bui_vwidth(), BUI_BORDER);
    }
}

static void draw_row(int i) {
    int x, y, w, h;
    int selected = ((int)i18n_selected() == i);
    int chip, cx, name_x, subtitle_y, name_y;

    row_geom(i, &x, &y, &w, &h);
    bui_round(x, y, w, h - 4, 12,
              selected ? BUI_SURFACE_HI : BUI_SURFACE,
              selected ? BUI_TEAL : BUI_SURFACE);

    chip = land() ? 32 : 40;
    cx = x + 12;
    bui_round(cx, y + (h - 4 - chip) / 2, chip, chip, 10,
              BUI_ACCENT_SOFT, BUI_ACCENT_SOFT);
    if (i == I18N_AUTO) {
        bui_icon(BUI_ICON_MONITOR, cx + chip / 2 - 8,
                 y + (h - 4) / 2 - 8, BUI_TEAL);
    } else {
        bui_text_center(i18n_code((enum i18n_lang)i),
                        cx + chip / 2, y + (h - 4) / 2 - (land() ? 3 : 7),
                        land() ? 1 : 2, BUI_TEAL);
    }

    name_x = cx + chip + 12;
    name_y = y + (land() ? 10 : 12);
    subtitle_y = y + (land() ? 32 : 36);
    bui_text(i18n_name((enum i18n_lang)i), name_x, name_y, 2,
             selected ? BUI_TEXT : BUI_TEXT);
    bui_text(i18n_native((enum i18n_lang)i), name_x, subtitle_y, 1, BUI_DIM);

    if (selected)
        bui_check(x + w - 30, y + (h - 4) / 2 - 6, BUI_TEAL);
}

void language_draw(void) {
    if (!g_vis) return;
    bui_begin();
    draw_header();
    for (int i = 0; i < LANG_ROWS; i++) draw_row(i);
    if (land())
        bui_text(i18n_t(I18N_LANG_HINT), 20, 336, 1, BUI_DIM);
    else
        bui_text(i18n_t(I18N_LANG_HINT), 20, 700, 1, BUI_DIM);
    bui_end();
}

int language_event(uint32_t type, uint32_t d1, uint32_t d2) {
    int vx, vy;
    if (!g_vis) return 0;
    if (type != (uint32_t)BUI_EV_TOUCH) return 0;
    bui_view_responsive();
    bui_map_touch((int)d1, (int)d2, &vx, &vy);
    for (int i = 0; i < LANG_ROWS; i++) {
        int x, y, w, h;
        row_geom(i, &x, &y, &w, &h);
        if (vx < x || vx >= x + w || vy < y || vy >= y + h - 4) continue;
        i18n_set((enum i18n_lang)i);
        return 1;
    }
    return 0;
}
