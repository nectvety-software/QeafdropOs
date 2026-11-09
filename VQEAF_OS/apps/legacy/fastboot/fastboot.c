/* SPDX-License-Identifier: MIT */
/* apps/fastboot/fastboot.c - "Fastboot Mode" screen, 360x780 virtual layout. */

#include "apps/fastboot/fastboot.h"

#include "apps/bootui/bootui.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/font_5x7.h"
#include "gui/window_manager.h"
#include "hal/include/hal.h"

#include <stddef.h>

#define FB_CARD_X   16
#define FB_CARD_Y   300
#define FB_CARD_W   328
#define FB_CARD_H   132

/* Six rows. The poster had five; the extra Flash Recovery row is compressed
 * into the same band between the info card (ends at 432) and the hint at 680,
 * so the waves at 720 are never covered. */
#define FB_PILL_X   16
#define FB_PILL_Y   440
#define FB_PILL_W   328
#define FB_PILL_H    42
#define FB_ROW_X    16
#define FB_ROW_Y    490
#define FB_ROW_W   328
#define FB_ROW_H    32
#define FB_ROW_STEP 36

struct fb_action_def {
    const char *label;
    enum fastboot_action act;
    int destructive;
    int icon;
};

static const struct fb_action_def g_actions[] = {
    { "Continue",      FASTBOOT_ACT_CONTINUE,       0, BUI_ICON_PLAY      },
    { "Recovery Mode", FASTBOOT_ACT_RECOVERY,       0, BUI_ICON_REBOOT    },
    { "Update Mode",   FASTBOOT_ACT_UPDATE,         0, BUI_ICON_DOWNLOAD  },
    /* A partition write. destructive=1 routes it through bui_confirm() with
     * CANCEL selected, so a stray Power press cannot reflash p77. */
    { "Flash Recovery", FASTBOOT_ACT_FLASH_RECOVERY, 1, BUI_ICON_DOWNLOAD  },
    { "Factory Reset", FASTBOOT_ACT_FACTORY_RESET,  1, BUI_ICON_GEAR      },
    { "Power Off",     FASTBOOT_ACT_POWER_OFF,      1, BUI_ICON_POWER     },
};

#define N_ACTIONS ((int)(sizeof(g_actions) / sizeof(g_actions[0])))

static const char *const g_action_names[FASTBOOT_ACT_COUNT] = {
    "NONE", "CONTINUE", "RECOVERY", "UPDATE", "FACTORY_RESET", "POWER_OFF",
    "FLASH RECOVERY (p77)"
};

struct fastboot_state {
    int visible;
    int selected;
    int confirm;
    int confirm_sel;                 /* 0 = CANCEL (safe default) */
    enum fastboot_action confirm_action;
    enum fastboot_action pending;
    char status[80];
    struct fastboot_info info;
};

static struct fastboot_state g;
static struct wm_window g_win;

static const char *const DEF_DEVICE  = "Redmi Note Pro";
static const char *const DEF_MODEL   = "VQEAF_RNP";
static const char *const DEF_VERSION = "v1.0.0.0";
static const char *const DEF_BUILD   = "2026-10-25_001";
static const char *const DEF_DATE    = "Sat Oct 25 2026 20:45:12";

static void copy_str(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int action_y(int i) {
    return i == 0 ? FB_PILL_Y : FB_ROW_Y + (i - 1) * FB_ROW_STEP;
}

static int action_h(int i) { return i == 0 ? FB_PILL_H : FB_ROW_H; }

static void enqueue(enum fastboot_action a) {
    g.pending = a;
    copy_str(g.status, sizeof(g.status), "Queued: ");
    size_t n = 8, j = 0;
    const char *name = fastboot_action_name(a);
    while (n + 1 < sizeof(g.status) && name[j]) g.status[n++] = name[j++];
    g.status[n] = '\0';
}

static void draw_info_card(void) {
    bui_round(FB_CARD_X, FB_CARD_Y, FB_CARD_W, FB_CARD_H, 12,
              BUI_SURFACE, BUI_BORDER);
    const char *keys[5] = { "Device", "Model", "Version", "Build", "Date" };
    const char *vals[5];
    vals[0] = g.info.device;
    vals[1] = g.info.model;
    vals[2] = g.info.version;
    vals[3] = g.info.build;
    vals[4] = g.info.date;
    for (int i = 0; i < 5; i++) {
        int y = FB_CARD_Y + 14 + i * 22;
        bui_text(keys[i], FB_CARD_X + 12, y, 1, BUI_DIM);
        bui_text(":", FB_CARD_X + 84, y, 1, BUI_DIM);
        bui_text(vals[i] ? vals[i] : "-", FB_CARD_X + 96, y, 1, BUI_TEXT);
    }
}

static void draw_menu(void) {
    for (int i = 0; i < N_ACTIONS; i++) {
        int y = action_y(i), h = action_h(i);
        int on = (i == g.selected);
        /* The poster gives Continue a pill and highlights the current row. */
        if (i == 0 || on)
            bui_round(FB_ROW_X, y, FB_ROW_W, h, h / 2,
                      on ? BUI_SURFACE_HI : BUI_SURFACE,
                      on ? BUI_TEAL : BUI_BORDER);
        bui_icon(g_actions[i].icon, 30, y + (h - 16) / 2, BUI_TEAL);
        bui_text(g_actions[i].label, 56, y + (h - 14) / 2, 2, BUI_TEXT);
        if (i == 0)
            bui_chevron(FB_PILL_X + FB_PILL_W - 24, y + (h - 12) / 2,
                        BUI_TEAL);
    }
}

static void draw_hint(void) {
    bui_text_center("Use Volume to navigate", 180, 680, 2, BUI_TEAL);
    bui_text_center("Press Power to select", 180, 702, 2, BUI_TEAL);
}

void fastboot_draw(void) {
    if (!g.visible) return;
    uint32_t w = gfx_width(), h = gfx_height();
    if (!w || !h || !gfx_fb()) return;

    bui_begin_fit();
    bui_topbar("Fastboot Mode");
    bui_vlogo(90, 88, 180, 125);
    bui_wordmark(180, 230, 5);
    bui_text_center("Your World, Your Rules.", 180, 272, 1, BUI_DIM);
    draw_info_card();
    bui_waves();
    draw_menu();
    draw_hint();
    if (g.confirm)
        bui_confirm(fastboot_action_name(g.confirm_action), g.confirm_sel);
    bui_end();
}

/* ------------------------------------------------------------------- events */

static int in_box(int vx, int vy, int x, int y, int w, int h) {
    return vx >= x && vx < x + w && vy >= y && vy < y + h;
}

static int finish_confirm(int accepted) {
    g.confirm = 0;
    if (accepted) {
        enqueue(g.confirm_action);
    } else {
        copy_str(g.status, sizeof(g.status), "Cancelled. Nothing was written.");
    }
    return 1;
}

static int activate_selected(int index) {
    if (index < 0 || index >= N_ACTIONS) return 0;
    if (g_actions[index].destructive) {
        g.confirm = 1;
        g.confirm_action = g_actions[index].act;
        g.confirm_sel = 0;
        copy_str(g.status, sizeof(g.status), "Confirmation required.");
    } else {
        enqueue(g_actions[index].act);
    }
    return 1;
}

int fastboot_event(uint32_t type, uint32_t d1, uint32_t d2) {
    if (!g.visible) return 0;

    if (g.confirm) {
        switch (type) {
        case BUI_EV_UP:
        case BUI_EV_DOWN:
            g.confirm_sel = 1 - g.confirm_sel;
            return 1;
        case BUI_EV_ACTIVATE:
        case BUI_EV_CONFIRM:
            return finish_confirm(g.confirm_sel);
        case BUI_EV_CANCEL:
            return finish_confirm(0);
        case BUI_EV_TOUCH: {
            int vx, vy;
            bui_map_touch((int)d1, (int)d2, &vx, &vy);
            if (in_box(vx, vy, BUI_BTN_C_X, BUI_BTN_Y, BUI_BTN_W, BUI_BTN_H))
                return finish_confirm(0);
            if (in_box(vx, vy, BUI_BTN_A_X, BUI_BTN_Y, BUI_BTN_W, BUI_BTN_H))
                return finish_confirm(1);
            return 1;
        }
        default:
            return 0;
        }
    }

    switch (type) {
    case BUI_EV_UP:
        g.selected = (g.selected - 1 + N_ACTIONS) % N_ACTIONS;
        return 1;
    case BUI_EV_DOWN:
        g.selected = (g.selected + 1) % N_ACTIONS;
        return 1;
    case BUI_EV_ACTIVATE:
        return activate_selected(g.selected);
    case BUI_EV_TOUCH: {
        int vx, vy;
            bui_map_touch((int)d1, (int)d2, &vx, &vy);
        for (int i = 0; i < N_ACTIONS; i++) {
            if (in_box(vx, vy, FB_ROW_X, action_y(i), FB_ROW_W,
                       action_h(i))) {
                g.selected = i;
                return activate_selected(i);
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}

void fastboot_poll_input(void) {
    static int was_pressed = 0;
    struct hal_touch *t = hal_touch();
    if (!g.visible || !t || !t->poll) return;
    int x = 0, y = 0, pressed = 0;
    if (t->poll(&x, &y, &pressed) != UMOS_OK) return;
    if (pressed && !was_pressed && x >= 0 && y >= 0)
        fastboot_event(BUI_EV_TOUCH, (uint32_t)x, (uint32_t)y);
    was_pressed = pressed ? 1 : 0;
}

/* -------------------------------------------------------------- public API */

static void fastboot_win_draw(struct wm_window *w) {
    (void)w;
    fastboot_draw();
}

static int fastboot_win_event(struct wm_window *w, uint32_t type,
                              uint32_t d1, uint32_t d2) {
    (void)w;
    return fastboot_event(type, d1, d2);
}

static void reset_state(void) {
    g.visible = 0;
    g.selected = 0;
    g.confirm = 0;
    g.confirm_sel = 0;
    g.confirm_action = FASTBOOT_ACT_NONE;
    g.pending = FASTBOOT_ACT_NONE;
    copy_str(g.status, sizeof(g.status), "Ready. No privileged action has run.");
    g.info.device = DEF_DEVICE;
    g.info.model = DEF_MODEL;
    g.info.version = DEF_VERSION;
    g.info.build = DEF_BUILD;
    g.info.date = DEF_DATE;
}

void fastboot_init(void) {
    reset_state();
    g_win.name = "fastboot";
    g_win.bounds = (struct umos_rect){0, 0, 1080, 2340};
    g_win.visible = 0;
    g_win.focused = 0;
    g_win.opaque = 1;
    g_win.draw = fastboot_win_draw;
    g_win.event = fastboot_win_event;
    wm_register_window(UMOS_APP_FASTBOOT, &g_win);
}

void fastboot_open(void) {
    g.visible = 1;
    wm_open(UMOS_APP_FASTBOOT);
}

void fastboot_close(void) {
    g.visible = 0;
    wm_close(UMOS_APP_FASTBOOT);
}

int fastboot_visible(void) { return g.visible; }
int fastboot_selected(void) { return g.selected; }
int fastboot_confirming(void) { return g.confirm; }
int fastboot_action_count(void) { return N_ACTIONS; }

void fastboot_set_info(const struct fastboot_info *info) {
    if (!info) return;
    g.info = *info;
    if (!g.info.device) g.info.device = DEF_DEVICE;
    if (!g.info.model) g.info.model = DEF_MODEL;
    if (!g.info.version) g.info.version = DEF_VERSION;
    if (!g.info.build) g.info.build = DEF_BUILD;
    if (!g.info.date) g.info.date = DEF_DATE;
}

void fastboot_set_status(const char *text) {
    copy_str(g.status, sizeof(g.status), text);
}

enum fastboot_action fastboot_take_action(void) {
    enum fastboot_action a = g.pending;
    g.pending = FASTBOOT_ACT_NONE;
    return a;
}

const char *fastboot_action_name(enum fastboot_action a) {
    if (a < 0 || a >= FASTBOOT_ACT_COUNT) return "UNKNOWN";
    return g_action_names[a];
}
