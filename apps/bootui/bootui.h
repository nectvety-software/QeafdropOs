/* SPDX-License-Identifier: MIT */
/* apps/bootui/bootui.h - Shared procedural art for the boot-mode screens.
 *
 * Both the Fastboot Mode and Recovery Mode screens are drawn in a 360x780
 * virtual space and scaled by K = gfx_width()/360 (K=3 on violet's 1080x2340
 * panel). Everything is integer arithmetic and RGB565 framebuffer writes: no
 * floats, no libm, no image decoder. The V mark, rings, waves and icons are
 * generated procedurally so the boot image needs no external assets.
 */

#ifndef UMOS_BOOTUI_H
#define UMOS_BOOTUI_H

#include <stdint.h>
#include "gui/gfx/gfx.h"

#define BUI_V_W 360
#define BUI_V_H 780

#define BUI_COL(r_, g_, b_) \
    ((struct umos_color){ (uint8_t)(r_), (uint8_t)(g_), (uint8_t)(b_), 0xFF })

/* Material dark palette (Pixel-style, AOSP 11 generation). Surfaces are
 * neutral greys instead of tinted glass, the accent is the familiar Pixel
 * blue, and every screen shares these eight tokens so the launcher, the lock
 * sheet and the settings pages read as one system. */
#define BUI_BG         BUI_COL(0x12, 0x12, 0x12)
#define BUI_BG_DEEP    BUI_COL(0x00, 0x00, 0x00)
#define BUI_SURFACE    BUI_COL(0x1E, 0x1E, 0x1E)
#define BUI_SURFACE_HI BUI_COL(0x2D, 0x2D, 0x2D)
#define BUI_BORDER     BUI_COL(0x3C, 0x40, 0x43)
#define BUI_ACCENT     BUI_COL(0x8A, 0xB4, 0xF8)
#define BUI_ACCENT_SOFT BUI_COL(0x1F, 0x35, 0x57)
/* Legacy token names kept so every existing screen picks up the new palette
 * without a mechanical rename: TEAL/CYAN now mean the accent and its tint. */
#define BUI_TEAL       BUI_ACCENT
#define BUI_TEAL_DIM   BUI_COL(0x66, 0x9D, 0xF6)
#define BUI_CYAN       BUI_COL(0xD2, 0xE3, 0xFC)
#define BUI_TEXT       BUI_COL(0xE8, 0xEA, 0xED)
#define BUI_DIM        BUI_COL(0x9A, 0xA0, 0xA6)
#define BUI_DANGER     BUI_COL(0xF2, 0x8B, 0x82)
/* Deliberately dark: the footer hint and Tips box sit on top of the waves,
 * so bright fills would swallow the text. Only the crests are bright. */
#define BUI_WAVE_1     BUI_COL(0x0B, 0x1A, 0x33)
#define BUI_WAVE_2     BUI_COL(0x12, 0x30, 0x5C)
#define BUI_WAVE_3     BUI_COL(0x1A, 0x73, 0xE8)

/* A sheet that covers the wallpaper does not repaint the panel: it lays a
 * plate over what the compositor already drew. This is how much of the plate
 * is poster navy; the rest is the blurred backdrop showing through. */
#define BUI_SHEET_ALPHA_Q 150

/* Icons are drawn inside a 16x16 virtual box at (x,y). */
enum bui_icon {
    BUI_ICON_PLAY = 0,
    BUI_ICON_REBOOT,
    BUI_ICON_DOWNLOAD,
    BUI_ICON_GEAR,
    BUI_ICON_POWER,
    BUI_ICON_DATABASE,
    BUI_ICON_TRASH,
    BUI_ICON_BROOM,
    BUI_ICON_SATELLITE,
    BUI_ICON_INFO,
    /* Launcher / system-bar glyphs. Appended, so the boot screens keep the
     * numeric ids they were drawn with. */
    BUI_ICON_PHONE,
    BUI_ICON_MESSAGE,
    BUI_ICON_GLOBE,
    BUI_ICON_CAMERA,
    BUI_ICON_PHOTO,
    BUI_ICON_PIN,
    BUI_ICON_MAIL,
    BUI_ICON_CLOCK,
    BUI_ICON_CALENDAR,
    BUI_ICON_CALC,
    BUI_ICON_FOLDER,
    BUI_ICON_SUN,
    BUI_ICON_CONTACTS,
    BUI_ICON_MIC,
    BUI_ICON_TERMINAL,
    BUI_ICON_SEARCH,
    BUI_ICON_WIFI,
    BUI_ICON_SIGNAL,
    BUI_ICON_LOCK,
    BUI_ICON_MUSIC,
    BUI_ICON_STORE,
    BUI_ICON_NOTES,
    BUI_ICON_FLASH,
    BUI_ICON_MONITOR,
    BUI_ICON_COUNT
};

/* Shared input events (identical numeric values on both screens). */
enum bui_event {
    BUI_EV_UP       = 1,
    BUI_EV_DOWN     = 2,
    BUI_EV_ACTIVATE = 3,
    BUI_EV_TOUCH    = 4,
    BUI_EV_CONFIRM  = 5,
    BUI_EV_CANCEL   = 6
};

/* Shared confirmation dialog geometry, so hit tests and drawing agree. */
#define BUI_DLG_X   24
#define BUI_DLG_Y   250
#define BUI_DLG_W   312
#define BUI_DLG_H   220
#define BUI_BTN_Y   (BUI_DLG_Y + BUI_DLG_H - 62)
#define BUI_BTN_H   42
#define BUI_BTN_W   132
#define BUI_BTN_C_X (BUI_DLG_X + 16)
#define BUI_BTN_A_X (BUI_DLG_X + BUI_DLG_W - 16 - BUI_BTN_W)

int  bui_scale(void);
/* Logical surface size measured in the 360x780 virtual unit. Landscape
 * panels are wider, so a rotation-aware screen reads these instead of
 * assuming 360x780. */
int  bui_vwidth(void);
int  bui_vheight(void);
/* Physical touch -> virtual coordinates, applying the inverse rotation and
 * the K scale in one step. Every screen should use this for hit tests. */
void bui_map_touch(int px, int py, int *vx, int *vy);
void bui_begin(void);
/* Opaque boot screens: keep the legacy 360x780 drawing space centred, even
 * when the panel is rotated, instead of reflowing a boot menu. */
void bui_begin_fit(void);
/* Like bui_begin(), but over the backdrop the compositor already painted:
 * the sheet's own ink lands on top and the blurred wallpaper stays visible.
 * On a panel that already holds BUI_BG the blend is exact, so screens drawn
 * without a backdrop look the same as before. */
void bui_begin_overlay(void);

/* Canvas selection for screens that paint their own backdrop. Portrait gives
 * 360x780, landscape 780x360; bui_view_fit() instead centres the 360x780
 * legacy design (boot menus, keyboard, Wi-Fi/Bluetooth pages). */
void bui_view_responsive(void);
void bui_view_fit(void);
/* Same 360x780 design at the fit scale, but with no centring origin: for
 * screens that already place themselves (the keyboard adds its own g_ox). */
void bui_view_legacy(void);
int  bui_landscape(void);
void bui_end(void);

void bui_fill(int x, int y, int w, int h, struct umos_color c);
void bui_hline(int x, int y, int w, struct umos_color c);
void bui_round(int x, int y, int w, int h, int r,
               struct umos_color fill, struct umos_color border);
void bui_gradient(int x, int y, int w, int h,
                  struct umos_color a, struct umos_color b);
void bui_dashed(int x, int y, int w, int h, struct umos_color c);

int  bui_text_w(const char *s, int scale);
void bui_text(const char *s, int x, int y, int scale, struct umos_color c);
void bui_text_center(const char *s, int cx, int y, int scale,
                     struct umos_color c);

void bui_topbar(const char *mode);
void bui_vlogo(int x, int y, int w, int h);
void bui_rings(int cx, int cy, int r);
void bui_wordmark(int cx, int y, int scale);
void bui_waves(void);
void bui_icon(int id, int x, int y, struct umos_color c);
void bui_chevron(int x, int y, struct umos_color c);
void bui_check(int x, int y, struct umos_color c);
void bui_confirm(const char *action, int cancel_selected);

/* ---- system bar ----------------------------------------------------------
 * The bar the launcher and the lock sheet draw over the wallpaper: a label on
 * the left and the battery on the right. battery_pct < 0 means the seam did
 * not answer, and the cell is drawn empty instead of inventing a level. No
 * wall clock exists on this board, so callers pass "--:--" rather than a time.
 */
void bui_statusbar(const char *label, int battery_pct);
void bui_battery(int x, int y, int battery_pct, struct umos_color c);

#endif
