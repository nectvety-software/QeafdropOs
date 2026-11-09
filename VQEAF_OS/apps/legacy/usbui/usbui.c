/* SPDX-License-Identifier: MIT */
/* apps/usbui/usbui.c - "Use USB for" sheet, 360x780 virtual.
 *
 * The reference behaviour is the familiar USB preference notification: plug the
 * phone into a PC and a sheet offers Charge / MTP / PTP. Here the same choice
 * is drawn with procedural integer art (no floats, no libm, no assets) and
 * queues an intent rather than touching the DWC3 gadget itself.
 *
 * Safety: no row is pre-selected. BACK and "Not now" both dismiss without any
 * mode change; only an explicit tap on one of the three rows queues SET_MODE.
 * The control LUN is never switched off by any of the three modes.
 */

#include "apps/usbui/usbui.h"

#include "apps/bootui/bootui.h"
#include "gui/gfx/gfx.h"

#include <stddef.h>

#define SHEET_X   16
#define SHEET_Y  210
#define SHEET_W  328
#define SHEET_H  372
#define ROW_X    28
#define ROW_W   304
#define ROW_Y0  300
#define ROW_H    52
#define ROW_STEP 58
#define N_ROWS    3
#define DISMISS_Y 492

static const char *const g_label[N_ROWS] = {
    "Charge this device",
    "Transfer files (MTP)",
    "Transfer photos (PTP)",
};
static const char *const g_sub[N_ROWS] = {
    "USB data off, power only",
    "Browse files on a computer",
    "Import photos on a computer",
};

static int g_vis;
static enum usbui_action g_act = USBUI_ACT_NONE;
static int g_arg;

/* ---- tiny procedural icons (16x16 virtual boxes) ------------------------ */

static void ic_battery(int x, int y, struct umos_color c) {
    bui_round(x + 1, y + 3, 12, 10, 2, BUI_SURFACE, c);
    bui_fill(x + 13, y + 6, 2, 4, c);
    bui_fill(x + 3, y + 5, 8, 6, c);
}

static void ic_files(int x, int y, struct umos_color c) {
    bui_round(x + 1, y + 2, 9, 12, 1, BUI_SURFACE, c);
    bui_round(x + 5, y + 1, 9, 12, 1, BUI_SURFACE, c);
    bui_fill(x + 7, y + 5, 5, 1, c);
    bui_fill(x + 7, y + 8, 5, 1, c);
}

static void ic_photo(int x, int y, struct umos_color c) {
    bui_round(x + 1, y + 2, 14, 12, 2, BUI_SURFACE, c);
    bui_fill(x + 3, y + 4, 10, 8, BUI_SURFACE_HI);
    bui_fill(x + 5, y + 9, 2, 2, c);
    bui_fill(x + 8, y + 7, 2, 4, c);
    bui_fill(x + 11, y + 5, 2, 6, c);
}

static void ic_close(int x, int y, struct umos_color c) {
    for (int i = 0; i < 9; i++) {
        bui_fill(x + i, y + i, 1, 1, c);
        bui_fill(x + 8 - i, y + i, 1, 1, c);
    }
}

static void disc(int cx, int cy, int r, struct umos_color c) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                bui_fill(cx + dx, cy + dy, 1, 1, c);
}

/* ---- drawing ------------------------------------------------------------- */

static void draw_sheet(void) {
    int cur = (int)usb_gadget_mode();

    bui_round(SHEET_X, SHEET_Y, SHEET_W, SHEET_H, 16,
              BUI_SURFACE, BUI_TEAL);
    bui_round(SHEET_X + 12, SHEET_Y - 12, 56, 24, 12, BUI_SURFACE_HI, BUI_BORDER);
    bui_text_center("USB", SHEET_X + 40, SHEET_Y - 6, 1, BUI_TEAL);

    bui_text("Use USB for", SHEET_X + 16, SHEET_Y + 16, 2, BUI_TEXT);
    bui_text("Connected to a computer", SHEET_X + 16, SHEET_Y + 40, 1, BUI_DIM);
    bui_hline(SHEET_X + 16, SHEET_Y + 58, SHEET_W - 32, BUI_BORDER);

    for (int i = 0; i < N_ROWS; i++) {
        int y = ROW_Y0 + i * ROW_STEP;
        int active = (i == cur);
        bui_round(ROW_X, y, ROW_W, ROW_H, 10,
                  active ? BUI_SURFACE_HI : BUI_SURFACE,
                  active ? BUI_TEAL : BUI_BORDER);
        if (i == 0) ic_battery(ROW_X + 12, y + 18, BUI_TEAL);
        else if (i == 1) ic_files(ROW_X + 12, y + 18, BUI_TEAL);
        else ic_photo(ROW_X + 12, y + 18, BUI_TEAL);
        bui_text(g_label[i], ROW_X + 42, y + 10, 1, BUI_TEXT);
        bui_text(g_sub[i], ROW_X + 42, y + 28, 1, BUI_DIM);
        if (active)
            disc(ROW_X + ROW_W - 20, y + ROW_H / 2, 4, BUI_TEAL);
    }

    bui_hline(SHEET_X + 16, DISMISS_Y - 12, SHEET_W - 32, BUI_BORDER);
    ic_close(SHEET_X + 20, DISMISS_Y + 4, BUI_DIM);
    bui_text("Not now", SHEET_X + 44, DISMISS_Y + 8, 1, BUI_DIM);
    bui_text("Rescue channel stays in every mode.",
             SHEET_X + 16, SHEET_Y + SHEET_H - 24, 1, BUI_DIM);
}

/* ---- input --------------------------------------------------------------- */

static int in_rect(int vx, int vy, int x, int y, int w, int h) {
    return vx >= x && vx < x + w && vy >= y && vy < y + h;
}

static void queue(enum usbui_action a, int arg) {
    g_act = a;
    g_arg = arg;
}

/* ---- public -------------------------------------------------------------- */

/* This sheet is an OVERLAY, not a WM window: it sits on top of whichever
 * screen was already visible instead of replacing it. init/main.c draws it
 * once per frame after compositor_frame(). */
void usbui_init(void) {
    g_vis = 0;
    g_act = USBUI_ACT_NONE;
    g_arg = 0;
}

void usbui_open(void) {
    g_vis = 1;
    g_act = USBUI_ACT_NONE;
}

void usbui_close(void) {
    g_vis = 0;
}

int usbui_visible(void) { return g_vis; }

void usbui_draw(void) {
    if (!g_vis) return;
    bui_begin_overlay();
    bui_view_fit();
    draw_sheet();
    bui_end();
}

int usbui_event(uint32_t type, uint32_t d1, uint32_t d2) {
    int vx, vy;

    if (!g_vis) return 0;
    if (type == USBUI_EV_BACK) { queue(USBUI_ACT_DISMISS, 0); return 1; }
    if (type != USBUI_EV_TOUCH) return 0;
    bui_map_touch((int)d1, (int)d2, &vx, &vy);
    for (int i = 0; i < N_ROWS; i++) {
        if (!in_rect(vx, vy, ROW_X, ROW_Y0 + i * ROW_STEP, ROW_W, ROW_H))
            continue;
        queue(USBUI_ACT_SET_MODE, USB_MODE_CHARGE + i);
        return 1;
    }
    if (in_rect(vx, vy, SHEET_X, DISMISS_Y - 12, SHEET_W, 40)) {
        queue(USBUI_ACT_DISMISS, 0);
        return 1;
    }
    return 1;                 /* taps outside the sheet do nothing */
}

enum usbui_action usbui_take_action(int *arg) {
    enum usbui_action a = g_act;
    if (arg) *arg = g_arg;
    g_act = USBUI_ACT_NONE;
    g_arg = 0;
    return a;
}

int usbui_row_count(void) { return N_ROWS; }
