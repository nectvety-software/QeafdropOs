/* SPDX-License-Identifier: MIT */
/* apps/settings/settings.c - Network + system settings hub.
 *
 * The reference screens are separate pages, so this window is the landing
 * list: Wi-Fi, Bluetooth, the Blueprint keyboard, display rotation, all
 * features and the system language. Each sub-screen keeps its own state and
 * intent queue; the hub only routes input and BACK. On a BACK the sub-screen
 * gets first refusal (details -> list), and only when it declines does the hub
 * take the user back to the hub.
 *
 * Portrait keeps the six-row list; landscape reflows it to two columns of
 * three so the same rows fit a 780x360 canvas.
 */

#include "apps/settings/settings.h"

#include "apps/bootui/bootui.h"
#include "apps/settings/btui.h"
#include "apps/settings/language.h"
#include "apps/settings/wifiui.h"
#include "apps/keyboard/keyboard.h"
#include "apps/settings/features.h"
#include "core/i18n.h"
#include "hal/include/display.h"
#include "gui/gfx/gfx.h"
#include "gui/window_manager.h"

#define HUB_ROWS    6
#define HUB_ROW_Y0 118
#define HUB_ROW_H   62
#define HUB_ROW_X   12
#define HUB_ROW_W  336

static struct wm_window g_settings_win;
static int g_page;      /* 0 hub, 1 wifi, 2 bt, 3 keyboard, 4 features, 5 language */
static int g_vis;

static const enum i18n_key HUB_KEYS[HUB_ROWS] = {
    I18N_WIFI, I18N_BT, I18N_KEYBOARD, I18N_ROTATION, I18N_FEATURES, I18N_LANGUAGE
};

static const char *const HUB_SUBS[HUB_ROWS] = {
    "Networks, details and preferences",
    "Paired devices and audio profiles",
    "Blueprint soft keyboard",
    "Orientation and auto-rotate",
    "UI, multitasking, security, AI",
    "Language and region"
};

static int isqrt_i(int v) {
    int lo = 0, hi = 1024;
    if (v <= 0) return 0;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (mid * mid <= v) lo = mid; else hi = mid - 1;
    }
    return lo;
}

static void ic_wifi_small(int x, int y, struct umos_color c) {
    for (int a = 1; a <= 3; a++) {
        int r = a * 5;
        for (int dx = -r; dx <= r; dx++) {
            int dy = isqrt_i(r * r - dx * dx);
            if (dy >= r - 1 && dx <= dy && -dx <= dy)
                bui_fill(x + 9 + dx, y + 13 - dy, 1, 1, c);
        }
    }
    bui_fill(x + 8, y + 12, 2, 2, c);
}

static void ic_bt_small(int x, int y, struct umos_color c) {
    int mid = x + 9;
    for (int i = 0; i <= 14; i++) bui_fill(mid, y + i, 1, 1, c);
    for (int i = 0; i <= 8; i++) {
        bui_fill(mid + i, y + i / 2, 1, 1, c);
        bui_fill(mid + 8 - i, y + 7 + i / 2, 1, 1, c);
        bui_fill(mid + i, y + 7 + i / 2, 1, 1, c);
        bui_fill(mid + 8 - i, y + 14 - i / 2, 1, 1, c);
    }
}

/* One geometry function for drawing and hit testing. */
static void hub_row_geom(int i, int *x, int *y, int *w, int *h) {
    if (bui_landscape()) {
        int col = i / 3;
        int row = i % 3;
        *x = 10 + col * 390;
        *y = 54 + row * 92;
        *w = 370;
        *h = 84;
    } else {
        *x = HUB_ROW_X;
        *y = HUB_ROW_Y0 + i * HUB_ROW_H;
        *w = HUB_ROW_W;
        *h = HUB_ROW_H - 8;
    }
}

static void draw_hub(void) {
    int land = bui_landscape();

    bui_text(i18n_t(I18N_SETTINGS), 16, land ? 10 : 14, 3, BUI_TEXT);
    bui_hline(0, land ? 44 : 56, bui_vwidth(), BUI_BORDER);
    if (!land) bui_text("Network & internet", 16, 74, 1, BUI_TEAL);

    for (int i = 0; i < HUB_ROWS; i++) {
        int x, y, w, h;
        int box = land ? 40 : 34;
        hub_row_geom(i, &x, &y, &w, &h);
        bui_round(x, y, w, h, 12, BUI_SURFACE, BUI_SURFACE);
        bui_round(x + 14, y + (h - box) / 2, box, box, box / 2,
                  BUI_ACCENT_SOFT, BUI_ACCENT_SOFT);
        if (i == 0)      ic_wifi_small(x + 14 + (box - 18) / 2, y + (h - box) / 2 + 9, BUI_TEAL);
        else if (i == 1) ic_bt_small(x + 14 + (box - 18) / 2, y + (h - box) / 2 + 9, BUI_TEAL);
        else bui_icon(i == 2 ? BUI_ICON_INFO
                      : i == 5 ? BUI_ICON_GLOBE : BUI_ICON_GEAR,
                      x + 14 + (box - 16) / 2, y + (h - box) / 2 + (box - 16) / 2,
                      BUI_TEAL);
        bui_text(i18n_t(HUB_KEYS[i]), x + 70, y + 8, 2, BUI_TEXT);
        bui_text(HUB_SUBS[i], x + 70, y + 30, 1, BUI_DIM);
        bui_chevron(x + w - 22, y + h / 2 - 7, BUI_DIM);
    }

    bui_text_center("VQEAF OS   |   System settings", bui_vwidth() / 2,
                    land ? 338 : 740, 1, BUI_DIM);
}

static void settings_win_draw(struct wm_window *w) {
    (void)w;
    settings_draw();
}

static int settings_win_event(struct wm_window *w, uint32_t type,
                              uint32_t d1, uint32_t d2) {
    (void)w;
    return settings_event(type, d1, d2);
}

void settings_init(void) {
    wifiui_init();
    btui_init();
    keyboard_init();
    features_init();
    language_init();
    g_page = 0;
    g_vis = 0;
    g_settings_win.name = "settings";
    g_settings_win.bounds = (struct umos_rect){0, 0, 1080, 2340};
    g_settings_win.visible = 0;
    g_settings_win.focused = 0;
    g_settings_win.draw = settings_win_draw;
    g_settings_win.event = settings_win_event;
    wm_register_window(UMOS_APP_SETTINGS, &g_settings_win);
}

void settings_open(void) {
    g_vis = 1;
    g_page = 0;
    wifiui_close();
    btui_close();
    keyboard_close();
    features_close();
    language_close();
    wm_open(UMOS_APP_SETTINGS);
}

void settings_draw(void) {
    if (!g_vis || !gfx_fb()) return;
    if (g_page == 1) { wifiui_draw(); return; }
    if (g_page == 2) { btui_draw(); return; }
    if (g_page == 3) { keyboard_draw(); return; }
    if (g_page == 4) { features_draw(); return; }
    if (g_page == 5) { language_draw(); return; }
    bui_begin();
    draw_hub();
    bui_end();
}

/* The keyboard only queues a rotation intent; the board applies it. */
static void drain_keyboard_action(void) {
    int arg = 0;
    if (keyboard_take_action(&arg) != KEYBOARD_ACT_ROTATE) return;
    if (arg == -2) display_set_auto_rotate(1);
    else {
        display_set_auto_rotate(0);
        display_set_orientation((enum display_orientation)arg);
    }
}

int settings_event(uint32_t type, uint32_t d1, uint32_t d2) {
    int vx, vy;

    if (!g_vis) return 0;

    if (g_page == 5) {
        if (type == (uint32_t)WIFIUI_EV_BACK) {
            language_close();
            g_page = 0;
            return 1;
        }
        return language_event(type, d1, d2);
    }
    if (g_page == 4) {
        int used = features_event(type, d1, d2);
        if (type == (uint32_t)WIFIUI_EV_BACK || !features_visible()) {
            features_close();
            g_page = 0;
            return 1;
        }
        return used;
    }
    if (g_page == 3) {
        int used = keyboard_event(type, d1, d2);
        drain_keyboard_action();
        if (type == (uint32_t)WIFIUI_EV_BACK ||
            !keyboard_visible()) {
            keyboard_close();
            g_page = 0;
            return 1;
        }
        return used;
    }
    if (g_page == 1 || g_page == 2) {
        int used = g_page == 1
            ? wifiui_event(type, d1, d2) : btui_event(type, d1, d2);
        if (used) return 1;
        /* The sub-screen declined: let it close and show the hub again. */
        if (type == (uint32_t)WIFIUI_EV_BACK ||
            type == (uint32_t)BTUI_EV_BACK) {
            if (g_page == 1) wifiui_close(); else btui_close();
            g_page = 0;
            return 1;
        }
        return 0;
    }

    if (type == (uint32_t)WIFIUI_EV_BACK ||
        type == (uint32_t)BTUI_EV_BACK) return 0;   /* hub has nowhere back */
    if (type != (uint32_t)WIFIUI_EV_TOUCH) return 0;
    bui_view_responsive();
    bui_map_touch((int)d1, (int)d2, &vx, &vy);
    for (int i = 0; i < HUB_ROWS; i++) {
        int x, y, w, h;
        hub_row_geom(i, &x, &y, &w, &h);
        if (vx < x || vx >= x + w) continue;
        if (vy < y || vy >= y + h) continue;
        if (i == 0)      { wifiui_open(); g_page = 1; }
        else if (i == 1) { btui_open();   g_page = 2; }
        else if (i == 2) { keyboard_open(); g_page = 3; }
        else if (i == 3) { keyboard_open(); g_page = 3; }
        else if (i == 4) { features_open(); g_page = 4; }
        else             { language_open(); g_page = 5; }
        return 1;
    }
    return 0;
}

int settings_page(void) { return g_page; }
