/* SPDX-License-Identifier: MIT */
/* apps/launcher/home.c - Launcher home screen, responsive.
 *
 * Home is the one screen that shows the wallpaper as the artist drew it: the
 * compositor paints it at zoom 1.0 with no blur, so nothing in this file paints
 * a full-screen rectangle. Everything below is a Material surface laid over
 * that backdrop: the system bar, an At a Glance line, a grid of rounded icon
 * tiles, the dock, the search pill and the gesture bar.
 *
 * Two orientations, one set of apps. Portrait is the 5x5 grid the phone
 * shipped with; landscape reflows to 9x3 and puts the search pill beside the
 * dock, so the same 25 entries fit a 780x360 canvas without clipping.
 *
 * The clock comes from core/clock.c: the RTC when the board answers, uptime
 * otherwise (the lock sheet labels which one). Capacity is the real
 * power_supply seam, sampled on a slow tick.
 */

#include "gui/window_manager.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/gfx.h"
#include "apps/bootui/bootui.h"
#include "hal/include/battery.h"
#include "core/clock.h"
#include "core/i18n.h"

static struct wm_window g_home_win;

struct home_app {
    const char *label;
    int icon;
    uint8_t r, g, b;
};

/* Tile ink follows the familiar per-app colour so the grid reads at a glance,
 * even though every glyph is procedural and every tile is the same shape. */
static const struct home_app APPS[25] = {
    { "Phone",      BUI_ICON_PHONE,    0x1E, 0x8E, 0x3E },
    { "Messages",   BUI_ICON_MESSAGE,  0x1A, 0x73, 0xE8 },
    { "Chrome",     BUI_ICON_GLOBE,    0xD9, 0x30, 0x25 },
    { "Camera",     BUI_ICON_CAMERA,   0x5F, 0x63, 0x68 },
    { "Photos",     BUI_ICON_PHOTO,    0xE3, 0x74, 0x00 },
    { "Play",       BUI_ICON_PLAY,     0x01, 0x88, 0x64 },
    { "Maps",       BUI_ICON_PIN,      0x1E, 0x8E, 0x3E },
    { "Mail",       BUI_ICON_MAIL,     0xD9, 0x30, 0x25 },
    { "Videos",     BUI_ICON_PLAY,     0xC5, 0x22, 0x1F },
    { "Clock",      BUI_ICON_CLOCK,    0x18, 0x6A, 0xE0 },
    { "Calendar",   BUI_ICON_CALENDAR, 0x1A, 0x73, 0xE8 },
    { "Calculator", BUI_ICON_CALC,     0x18, 0x6A, 0xE0 },
    { "Files",      BUI_ICON_FOLDER,   0x18, 0x6A, 0xE0 },
    { "Settings",   BUI_ICON_GEAR,     0x5F, 0x63, 0x68 },
    { "Weather",    BUI_ICON_SUN,      0xE3, 0x74, 0x00 },
    { "Gallery",    BUI_ICON_PHOTO,    0x1E, 0x8E, 0x3E },
    { "Contacts",   BUI_ICON_CONTACTS, 0x1A, 0x73, 0xE8 },
    { "Recorder",   BUI_ICON_MIC,      0xD9, 0x30, 0x25 },
    { "Radio",      BUI_ICON_WIFI,     0xE3, 0x74, 0x00 },
    { "Terminal",   BUI_ICON_TERMINAL, 0x20, 0x21, 0x24 },
    { "Store",      BUI_ICON_STORE,    0x1E, 0x8E, 0x3E },
    { "Music",      BUI_ICON_MUSIC,    0xD9, 0x30, 0x25 },
    { "Notes",      BUI_ICON_NOTES,    0xE3, 0x74, 0x00 },
    { "Browser",    BUI_ICON_GLOBE,    0x1A, 0x73, 0xE8 },
    { "About",      BUI_ICON_INFO,     0x5F, 0x63, 0x68 },
};

/* The dock repeats the four most-used apps of the grid. */
static const int DOCK_APPS[4] = { 0, 1, 2, 3 };

/* Portrait geometry (360x780). */
#define GRID_COLS 5
#define CELL_W    72
#define CELL_H    82
#define GRID_Y0   116
#define TILE      42
#define TILE_R    12
#define LABEL_DY  46
#define DOCK_Y    560
#define DOCK_H    76
#define PILL_Y    652
#define PILL_H    46

static int g_bat = -1;
static int g_bat_tick;

static void sample_battery(void) {
    struct battery_state b;
    if (g_bat_tick > 0) { g_bat_tick--; return; }
    g_bat_tick = 32;
    g_bat = battery_read(&b) == 0 ? (int)b.capacity : -1;
}

static struct umos_color ink(uint8_t r, uint8_t g, uint8_t b) {
    struct umos_color c = { r, g, b, 0xFF };
    return c;
}

static void tile(const struct home_app *a, int cx, int y, int size, int radius) {
    struct umos_color fill = { a->r, a->g, a->b, 0xFF };
    bui_round(cx - size / 2, y, size, size, radius, fill, fill);
    bui_icon(a->icon, cx - 8, y + (size - 16) / 2, ink(0xFF, 0xFF, 0xFF));
}

static void draw_portrait(void) {
    int i;

    bui_text(i18n_t(I18N_GLANCE), 20, 42, 2, BUI_TEXT);
    bui_text("VQEAF OS 0.1.0", 20, 66, 1, BUI_DIM);

    for (i = 0; i < 25; i++) {
        int cx = (i % GRID_COLS) * CELL_W + CELL_W / 2;
        int y = GRID_Y0 + (i / GRID_COLS) * CELL_H;
        tile(&APPS[i], cx, y, TILE, TILE_R);
        bui_text_center(APPS[i].label, cx, y + LABEL_DY, 1, BUI_TEXT);
    }

    /* three pages, this one current */
    bui_fill(172, 522, 3, 3, BUI_TEXT);
    bui_fill(180, 522, 3, 3, BUI_DIM);
    bui_fill(188, 522, 3, 3, BUI_DIM);

    bui_round(8, DOCK_Y, 344, DOCK_H, 26, BUI_SURFACE, BUI_BORDER);
    for (i = 0; i < 4; i++) {
        int cx = 60 + i * 80;
        tile(&APPS[DOCK_APPS[i]], cx, DOCK_Y + 16, 44, 14);
    }

    bui_round(20, PILL_Y, 320, PILL_H, 23, BUI_SURFACE_HI, BUI_SURFACE_HI);
    bui_icon(BUI_ICON_SEARCH, 36, PILL_Y + 15, BUI_DIM);
    bui_text(i18n_t(I18N_SEARCH), 60, PILL_Y + 17, 2, BUI_DIM);
    bui_icon(BUI_ICON_MIC, 308, PILL_Y + 15, BUI_TEAL);

    bui_round(130, 716, 100, 4, 2, BUI_TEXT, BUI_TEXT);
}

static void draw_landscape(void) {
    int i;

    /* At a Glance sits on one line; the build tag keeps the right corner. */
    bui_text(i18n_t(I18N_GLANCE), 16, 38, 2, BUI_TEXT);
    bui_text("VQEAF OS 0.1.0", 16, 58, 1, BUI_DIM);

    /* 9x3 grid: 25 entries, 36 px tiles, labels under each. */
    for (i = 0; i < 25; i++) {
        int cx = 12 + (i % 9) * 84 + 42;
        int y = 76 + (i / 9) * 62;
        tile(&APPS[i], cx, y, 36, 10);
        bui_text_center(APPS[i].label, cx, y + 40, 1, BUI_TEXT);
    }

    /* Search pill on the left, dock on the right, one row. */
    bui_round(12, 276, 300, 40, 20, BUI_SURFACE_HI, BUI_SURFACE_HI);
    bui_icon(BUI_ICON_SEARCH, 28, 286, BUI_DIM);
    bui_text(i18n_t(I18N_SEARCH), 52, 288, 2, BUI_DIM);
    bui_icon(BUI_ICON_MIC, 288, 286, BUI_TEAL);

    bui_round(424, 270, 344, 52, 22, BUI_SURFACE, BUI_BORDER);
    for (i = 0; i < 4; i++) {
        int cx = 470 + i * 84;
        tile(&APPS[DOCK_APPS[i]], cx, 280, 36, 11);
    }

    bui_round(340, 344, 100, 4, 2, BUI_TEXT, BUI_TEXT);
}

static void home_draw(struct wm_window *w) {
    char now[8];
    (void)w;

    sample_battery();
    bui_view_responsive();
    clock_hhmm(now, (int)sizeof(now));
    bui_statusbar(now, g_bat);

    if (bui_landscape()) draw_landscape();
    else draw_portrait();
}

static int home_event(struct wm_window *w, uint32_t type, uint32_t d1, uint32_t d2) {
    (void)w; (void)type; (void)d1; (void)d2;
    return 0;
}

void home_init(void) {
    g_home_win.name = "home";
    g_home_win.bounds = (struct umos_rect){0, 0, 1080, 2340};
    g_home_win.visible = 0;
    g_home_win.focused = 0;
    g_home_win.draw = home_draw;
    g_home_win.event = home_event;
    g_bat = -1;
    g_bat_tick = 0;
    wm_register_window(UMOS_APP_HOME, &g_home_win);
}
