/* SPDX-License-Identifier: MIT */
/* apps/recovery/recovery.c - VQEAF OS "Recovery Mode" screen.
 *
 * Poster layout: brand bar, ringed V mark, VQEAF OS wordmark + Recovery
 * subtitle, six rounded menu rows, a dashed Tips box and the teal wave footer.
 *
 * The safety model is unchanged: the GUI never performs a privileged action.
 * It collects an intent, asks for confirmation on destructive ones, and leaves
 * execution to the board / rescue shell through recovery_take_action(). Only
 * intents ever exist here; no partition is written from this file.
 */

#include "apps/recovery/recovery.h"

#include "apps/bootui/bootui.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/font_5x7.h"
#include "gui/window_manager.h"
#include "hal/include/hal.h"

#include <stddef.h>

#define RC_RING_CX  180
#define RC_RING_CY  150
#define RC_RING_R   88

#define RC_LIST_X   16
#define RC_LIST_Y   318
#define RC_ROW_W    328
#define RC_ROW_H    46
#define RC_ROW_STEP 52

#define RC_TIPS_X   16
#define RC_TIPS_Y   636
#define RC_TIPS_W   328
#define RC_TIPS_H   74

struct action_def {
    const char *label;
    const char *hint;
    enum recovery_action act;
    int destructive;
    int icon;
};

static const struct action_def g_actions[] = {
    { "Reboot System",    "Boot the system slot",        RECOVERY_ACT_REBOOT,        0, BUI_ICON_DATABASE  },
    { "Apply Update",     "BOOTIMG.BIN -> p77 (confirm)", RECOVERY_ACT_FLASH,         1, BUI_ICON_DOWNLOAD },
    { "Wipe Data",        "User data reset",             RECOVERY_ACT_WIPE_DATA,     1, BUI_ICON_TRASH    },
    { "Wipe Cache",       "Cache partition reset",       RECOVERY_ACT_WIPE_CACHE,    1, BUI_ICON_BROOM    },
    { "Advanced Options", "Boot log / rescue channel",   RECOVERY_ACT_BOOT_LOG,      0, BUI_ICON_SATELLITE},
    { "Power Off",        "Shut the device down",         RECOVERY_ACT_POWEROFF,      1, BUI_ICON_POWER    },
};

#define N_ACTIONS ((int)(sizeof(g_actions) / sizeof(g_actions[0])))

static const char *const g_action_names[RECOVERY_ACT_COUNT] = {
    "NONE", "REBOOT", "BOOTLOADER", "POWEROFF", "FLASH",
    "BOOT_LOG", "DISPLAY_TEST", "RESCUE_STATUS", "BACK",
    "WIPE_DATA", "WIPE_CACHE"
};

struct recovery_state {
    int visible;
    int selected;
    int confirm;
    int confirm_sel;                 /* 0 = CANCEL (safe default) */
    enum recovery_action confirm_action;
    enum recovery_action pending;
    int progress;                    /* -1 = hidden */
    char status[96];
    struct recovery_info info;
};

static struct recovery_state g;
static struct wm_window g_win;

static const char *const DEF_VERSION = "VQEAF OS 0.1.0";
static const char *const DEF_DEVICE  = "violet / Redmi Note 7 Pro";
static const char *const DEF_SLOT    = "p77 recovery";
static const char *const DEF_IMAGE   = "m46";
static const char *const DEF_USB     = "QEAFRESCUE 32 MiB";
static const char *const DEF_CLOCK   = "18:48";

static void copy_str(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void enqueue(enum recovery_action a) {
    g.pending = a;
    copy_str(g.status, sizeof(g.status), "Queued: ");
    size_t n = 8, j = 0;
    const char *name = recovery_action_name(a);
    while (n + 1 < sizeof(g.status) && name[j]) g.status[n++] = name[j++];
    g.status[n] = '\0';
    g.progress = (a == RECOVERY_ACT_FLASH) ? 0 : -1;
}

/* ------------------------------------------------------------------ drawing */

static void draw_brand(void) {
    bui_rings(RC_RING_CX, RC_RING_CY, RC_RING_R);
    bui_vlogo(125, 112, 110, 76);
    bui_wordmark(180, 248, 4);
    bui_text_center("Recovery", 180, 284, 2, BUI_CYAN);
}

static void draw_action_list(void) {
    for (int i = 0; i < N_ACTIONS; i++) {
        int y = RC_LIST_Y + i * RC_ROW_STEP;
        int on = (i == g.selected);
        bui_round(RC_LIST_X, y, RC_ROW_W, RC_ROW_H, 10,
                  on ? BUI_SURFACE_HI : BUI_SURFACE,
                  on ? BUI_TEAL : BUI_BORDER);
        bui_icon(g_actions[i].icon, RC_LIST_X + 14, y + (RC_ROW_H - 16) / 2,
                 on ? BUI_TEAL : BUI_TEAL_DIM);
        bui_text(g_actions[i].label, RC_LIST_X + 44, y + (RC_ROW_H - 14) / 2,
                 2, BUI_TEXT);
        bui_chevron(RC_LIST_X + RC_ROW_W - 22, y + (RC_ROW_H - 12) / 2,
                    on ? BUI_TEAL : BUI_DIM);
    }
}

static void draw_tips(void) {
    bui_dashed(RC_TIPS_X, RC_TIPS_Y, RC_TIPS_W, RC_TIPS_H, BUI_BORDER);
    bui_icon(BUI_ICON_INFO, RC_TIPS_X + 12, RC_TIPS_Y + 12, BUI_TEAL);
    bui_text("Tips:", RC_TIPS_X + 36, RC_TIPS_Y + 14, 2, BUI_TEAL);
    bui_text_center("Use Volume to navigate", 180, RC_TIPS_Y + 38, 1, BUI_DIM);
    bui_text_center("Press Power to select", 180, RC_TIPS_Y + 54, 1, BUI_DIM);
}

static void draw_footer(void) {
    bui_text_center("VQEAF OS   |   Recovery v1.0", 180, 720, 1, BUI_DIM);
    bui_text_center(g.status[0] ? g.status : "Ready.", 180, 738, 1, BUI_DIM);
    if (g.progress >= 0) {
        int p = g.progress > 100 ? 100 : g.progress;
        bui_round(16, 758, 328, 6, 3, BUI_SURFACE, BUI_BORDER);
        if (p > 0) bui_fill(16, 758, (328 * p) / 100, 6, BUI_TEAL);
    }
}

void recovery_draw(void) {
    if (!g.visible) return;
    uint32_t w = gfx_width(), h = gfx_height();
    if (!w || !h || !gfx_fb()) return;

    bui_begin_fit();
    bui_topbar("Recovery Mode");
    draw_brand();
    bui_waves();
    draw_action_list();
    draw_tips();
    draw_footer();
    if (g.confirm)
        bui_confirm(recovery_action_name(g.confirm_action), g.confirm_sel);
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
        g.progress = -1;
    }
    return 1;
}

static int activate_selected(int index) {
    if (index < 0 || index >= N_ACTIONS) return 0;
    if (g_actions[index].destructive) {
        g.confirm = 1;
        g.confirm_action = g_actions[index].act;
        g.confirm_sel = 0;             /* safe default: CANCEL */
        copy_str(g.status, sizeof(g.status), "Confirmation required.");
        g.progress = -1;
    } else {
        enqueue(g_actions[index].act);
    }
    return 1;
}

int recovery_event(uint32_t type, uint32_t d1, uint32_t d2) {
    if (!g.visible) return 0;

    if (g.confirm) {
        switch (type) {
        case RECOVERY_EV_UP:
        case RECOVERY_EV_DOWN:
            g.confirm_sel = 1 - g.confirm_sel;
            return 1;
        case RECOVERY_EV_ACTIVATE:
        case RECOVERY_EV_CONFIRM:
            return finish_confirm(g.confirm_sel);
        case RECOVERY_EV_CANCEL:
            return finish_confirm(0);
        case RECOVERY_EV_TOUCH: {
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
    case RECOVERY_EV_UP:
        g.selected = (g.selected - 1 + N_ACTIONS) % N_ACTIONS;
        return 1;
    case RECOVERY_EV_DOWN:
        g.selected = (g.selected + 1) % N_ACTIONS;
        return 1;
    case RECOVERY_EV_ACTIVATE:
        return activate_selected(g.selected);
    case RECOVERY_EV_TOUCH: {
        int vx, vy;
            bui_map_touch((int)d1, (int)d2, &vx, &vy);
        for (int i = 0; i < N_ACTIONS; i++) {
            int y = RC_LIST_Y + i * RC_ROW_STEP;
            if (in_box(vx, vy, RC_LIST_X, y, RC_ROW_W, RC_ROW_H)) {
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

void recovery_poll_input(void) {
    static int was_pressed = 0;
    struct hal_touch *t = hal_touch();
    if (!g.visible || !t || !t->poll) return;
    int x = 0, y = 0, pressed = 0;
    if (t->poll(&x, &y, &pressed) != UMOS_OK) return;
    if (pressed && !was_pressed && x >= 0 && y >= 0)
        recovery_event(RECOVERY_EV_TOUCH, (uint32_t)x, (uint32_t)y);
    was_pressed = pressed ? 1 : 0;
}

/* -------------------------------------------------------------- public API */

static void recovery_win_draw(struct wm_window *w) {
    (void)w;
    recovery_draw();
}

static int recovery_win_event(struct wm_window *w, uint32_t type,
                              uint32_t d1, uint32_t d2) {
    (void)w;
    return recovery_event(type, d1, d2);
}

static void reset_state(void) {
    g.visible = 0;
    g.selected = 0;
    g.confirm = 0;
    g.confirm_sel = 0;
    g.confirm_action = RECOVERY_ACT_NONE;
    g.pending = RECOVERY_ACT_NONE;
    g.progress = -1;
    copy_str(g.status, sizeof(g.status), "Ready.");
    g.info.version = DEF_VERSION;
    g.info.device = DEF_DEVICE;
    g.info.slot = DEF_SLOT;
    g.info.image = DEF_IMAGE;
    g.info.usb = DEF_USB;
    g.info.clock = DEF_CLOCK;
    g.info.battery = 100;
    g.info.signal_bars = 4;
}

void recovery_init(void) {
    reset_state();
    g_win.name = "recovery";
    g_win.bounds = (struct umos_rect){0, 0, 1080, 2340};
    g_win.visible = 0;
    g_win.focused = 0;
    g_win.opaque = 1;
    g_win.draw = recovery_win_draw;
    g_win.event = recovery_win_event;
    wm_register_window(UMOS_APP_RECOVERY, &g_win);
}

void recovery_open(void) {
    g.visible = 1;
    wm_open(UMOS_APP_RECOVERY);
}

void recovery_close(void) {
    g.visible = 0;
    wm_close(UMOS_APP_RECOVERY);
}

int recovery_visible(void) { return g.visible; }
int recovery_selected(void) { return g.selected; }
int recovery_confirming(void) { return g.confirm; }
int recovery_action_count(void) { return N_ACTIONS; }

void recovery_set_info(const struct recovery_info *info) {
    if (!info) return;
    g.info = *info;
    if (g.info.battery < 0) g.info.battery = 0;
    if (g.info.battery > 100) g.info.battery = 100;
    if (g.info.signal_bars < 0) g.info.signal_bars = 0;
    if (g.info.signal_bars > 4) g.info.signal_bars = 4;
    if (!g.info.version) g.info.version = DEF_VERSION;
    if (!g.info.device) g.info.device = DEF_DEVICE;
    if (!g.info.slot) g.info.slot = DEF_SLOT;
    if (!g.info.image) g.info.image = DEF_IMAGE;
    if (!g.info.usb) g.info.usb = DEF_USB;
    if (!g.info.clock) g.info.clock = DEF_CLOCK;
}

void recovery_set_status(const char *text) {
    copy_str(g.status, sizeof(g.status), text);
}

void recovery_set_progress(int pct) {
    g.progress = pct < 0 ? -1 : (pct > 100 ? 100 : pct);
}

enum recovery_action recovery_take_action(void) {
    enum recovery_action a = g.pending;
    g.pending = RECOVERY_ACT_NONE;
    return a;
}

const char *recovery_action_name(enum recovery_action a) {
    if (a < 0 || a >= RECOVERY_ACT_COUNT) return "UNKNOWN";
    return g_action_names[a];
}
