/* SPDX-License-Identifier: MIT */
/* apps/settings/features.c - live feature status, 360x780 virtual.
 *
 * Tapping a row ACTUALLY changes the underlying subsystem (dark mode,
 * wallpaper palette, split screen, widget resize, AI intent), so the page is
 * a control surface and not a marketing list.
 */

#include "apps/settings/features.h"

#include "apps/bootui/bootui.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/wallpaper.h"
#include "gui/widgets/widget_grid.h"
#include "gui/window_manager.h"
#include "core/platform.h"

/* ---- tiny string builders (the freestanding link has no snprintf) --------- */

static void st_set(char *out, int cap, const char *s) {
    int n = 0;
    while (*s && n + 1 < cap) out[n++] = *s++;
    out[n] = '\0';
}

static void st_add(char *out, int cap, const char *s) {
    int n = 0;
    while (out[n] && n < cap) n++;
    while (*s && n + 1 < cap) out[n++] = *s++;
    out[n] = '\0';
}

static void st_int(char *out, int cap, int v) {
    char t[12];
    int k = 0;
    if (v == 0) { st_add(out, cap, "0"); return; }
    if (v < 0) { st_add(out, cap, "-"); v = -v; }
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k > 0) { char one[2]; one[0] = t[--k]; one[1] = '\0';
                    st_add(out, cap, one); }
}

#define ROW_X   12
#define ROW_W  336
#define ROW_H   34
#define ROW_Y0  58

struct feat_row {
    const char *name;
    int group;                 /* 0..3 */
    int tap_action;            /* 0 = display only */
};

enum {
    ACT_NONE = 0,
    ACT_DARK,
    ACT_DYNAMIC,
    ACT_ICON_PACK,
    ACT_FONT_SCALE,
    ACT_SPLIT,
    ACT_FLOAT,
    ACT_WIDGET,
    ACT_ADAPTIVE,
    ACT_AI
};

/* 4 groups, 14 rows. Group headers are drawn inline before their first row. */
static const struct feat_row ROWS[] = {
    {"Widgets (resize)",        0, ACT_WIDGET},
    {"Dynamic color",           0, ACT_DYNAMIC},
    {"Dark theme (OLED)",       0, ACT_DARK},
    {"Icon pack",               0, ACT_ICON_PACK},
    {"Font scale",              0, ACT_FONT_SCALE},
    {"Split screen",            1, ACT_SPLIT},
    {"Floating windows",        1, ACT_FLOAT},
    {"Adaptive RAM / battery",  1, ACT_ADAPTIVE},
    {"Wi-Fi / Bluetooth",       2, 0},
    {"NFC / UWB",               2, 0},
    {"App sandbox",             2, 0},
    {"Permissions",             2, 0},
    {"Theft protection",        2, 0},
    {"AI assistant",            3, ACT_AI},
};
#define N_ROWS ((int)(sizeof(ROWS) / sizeof(ROWS[0])))

static const char *const GROUPS[4] = {
    "UI & CUSTOMIZATION", "MULTITASK & PERFORMANCE",
    "CONNECT / SECURITY", "INTELLIGENCE"
};

static int g_vis;
static int g_widget_id = -1;

/* ---- status strings, all from real getters -------------------------------- */

static void row_status(int i, char *out, int cap) {
    out[0] = '\0';
    switch (i) {
    case 0:
        if (widget_grid_count()) { st_int(out, cap, widget_grid_count());
                                   st_add(out, cap, " placed"); }
        else st_set(out, cap, "add widget");
        break;
    case 1: st_set(out, cap, theme_dynamic() ? "wallpaper ON" : "tap to sample"); break;
    case 2: st_set(out, cap, theme_mode() == THEME_DARK ? "OLED black ON" : "light"); break;
    case 3: st_set(out, cap, theme_icon_pack_name(theme_icon_pack())); break;
    case 4: st_add(out, cap, "x"); st_int(out, cap, theme_font_scale()); break;
    case 5: st_set(out, cap, wm_split_active() ? "2 apps" : "off"); break;
    case 6: st_int(out, cap, wm_float_count()); st_add(out, cap, " open"); break;
    case 7: st_int(out, cap, platform_frozen_count());
            st_add(out, cap, " frozen / rules"); break;
    case 8: {
        uint32_t c = platform_connectivity();
        if (c & CONN_WIFI) st_add(out, cap, "WiFi ");
        if (c & CONN_BT)   st_add(out, cap, "BT ");
        if (c & CONN_USB)  st_add(out, cap, "USB");
        if (!out[0]) st_set(out, cap, "all idle");
        break;
    }
    case 9:  st_set(out, cap, "no HW (violet)"); break;
    case 10: st_int(out, cap, platform_sandbox_denials());
             st_add(out, cap, " denied"); break;
    case 11: st_int(out, cap, platform_alerts());
             st_add(out, cap, " alerts"); break;
    case 12: st_set(out, cap, platform_locked() ? "LOCKED" : "armed"); break;
    case 13: st_set(out, cap, "host only / ");
             st_int(out, cap, platform_ai_pending()); break;
    default: st_set(out, cap, "-"); break;
    }
}

static void draw_rows(void) {
    int y = ROW_Y0;
    int last_group = -1;
    for (int i = 0; i < N_ROWS; i++) {
        char status[40];
        if (ROWS[i].group != last_group) {
            last_group = ROWS[i].group;
            bui_text(GROUPS[last_group], ROW_X + 4, y + 2, 1, BUI_TEAL);
            y += 22;
        }
        bui_round(ROW_X, y, ROW_W, ROW_H - 4, 7, BUI_SURFACE, BUI_BORDER);
        bui_text(ROWS[i].name, ROW_X + 10, y + 8, 1, BUI_TEXT);
        row_status(i, status, sizeof(status));
        {
            int tw = bui_text_w(status, 1);
            bui_text(status, ROW_X + ROW_W - 12 - tw, y + 8, 1,
                     ROWS[i].tap_action ? BUI_TEAL : BUI_DIM);
        }
        y += ROW_H;
    }
    bui_text("Tap a teal row to change it. BACK returns to Settings.",
             ROW_X + 4, y + 6, 1, BUI_DIM);
}

void features_init(void) { g_vis = 0; g_widget_id = -1; }
void features_open(void) { g_vis = 1; }
void features_close(void) { g_vis = 0; }
int features_visible(void) { return g_vis; }

void features_draw(void) {
    if (!g_vis) return;
    bui_begin_fit();
    bui_text("Features", 16, 18, 3, BUI_TEXT);
    bui_hline(0, 50, BUI_V_W, BUI_BORDER);
    draw_rows();
    bui_end();
}

static void do_action(int act) {
    switch (act) {
    case ACT_DARK:
        theme_set_mode(theme_mode() == THEME_DARK ? THEME_LIGHT : THEME_DARK);
        break;
    case ACT_DYNAMIC:
        theme_apply_wallpaper();
        break;
    case ACT_ICON_PACK:
        theme_set_icon_pack((theme_icon_pack() + 1) % THEME_ICON_PACKS);
        break;
    case ACT_FONT_SCALE:
        theme_set_font_scale(theme_font_scale() >= THEME_FONT_SCALE_MAX
                             ? THEME_FONT_SCALE_MIN : theme_font_scale() + 1);
        break;
    case ACT_SPLIT:
        if (wm_split_active()) wm_split_end();
        else wm_set_split(UMOS_APP_MESSAGES, UMOS_APP_BROWSER, 128);
        break;
    case ACT_FLOAT:
        if (wm_float_count()) wm_float_remove(UMOS_APP_MESSAGES);
        else wm_float_add(UMOS_APP_MESSAGES, 40, 300, 280, 220);
        break;
    case ACT_WIDGET:
        if (g_widget_id < 0)
            g_widget_id = widget_grid_add(WG_CLOCK, 0, 0, 2, 1);
        else if (widget_grid_resize(g_widget_id, 3, 1) != 0)
            widget_grid_resize(g_widget_id, 1, 1);
        break;
    case ACT_ADAPTIVE:
        /* A demo wake: real counters, real freeze decision. */
        platform_record_usage(UMOS_APP_GALLERY, 0, 4);
        platform_adaptive_tick(120, 1, 0);
        break;
    case ACT_AI:
        platform_ai_enqueue("summarise battery usage");
        break;
    default:
        break;
    }
}

int features_event(uint32_t type, uint32_t d1, uint32_t d2) {
    int vx, vy, y, last_group = -1;
    if (!g_vis) return 0;
    if (type == 2) { features_close(); return 1; }   /* BACK */
    if (type != 1 && type != BUI_EV_TOUCH) return 0;
    bui_map_touch((int)d1, (int)d2, &vx, &vy);
    y = ROW_Y0;
    for (int i = 0; i < N_ROWS; i++) {
        if (ROWS[i].group != last_group) { last_group = ROWS[i].group; y += 22; }
        if (vx >= ROW_X && vx < ROW_X + ROW_W &&
            vy >= y && vy < y + ROW_H - 4) {
            if (ROWS[i].tap_action) do_action(ROWS[i].tap_action);
            return 1;
        }
        y += ROW_H;
    }
    return 1;
}
