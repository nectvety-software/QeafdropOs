/* SPDX-License-Identifier: MIT
 * apps/installer/main.c - QeafdropOs App Installer
 *
 * A light-mode app store/installer UI for QeafdropOs. Features:
 *  - Search bar at the top with clear button
 *  - Category tabs: Tất cả | Game | Ứng dụng
 *  - Scrollable app list with cards: icon, name, size, rating, install button
 *  - Install progress indicator
 *  - App details page with download button, size, version, developer, description
 *
 * Build with QeafdropOs (homo-init target) for the violet phone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <linux/input.h>

/* ── DRM ioctl definitions (self-contained, no libdrm) ────────────── */

#include <drm/drm.h>
#include <drm/drm_fourcc.h>

#define DRIVER "/dev/dri/card0"
#define TOUCH  "/dev/input/event1"
#define WIDTH  1080
#define HEIGHT 2340

/* ── Light theme palette ──────────────────────────────────────────── */

static uint32_t COL_BG       = 0xFFF5F3EE;
static uint32_t COL_CARD     = 0xFFFFFFFF;
static uint32_t COL_TEXT     = 0xFF1C1712;
static uint32_t COL_SUB      = 0xFF8E8E93;
static uint32_t COL_ACCENT   = 0xFF2C7BEA;
static uint32_t COL_BORDER   = 0xFFE5E5EA;
static uint32_t COL_WHITE    = 0xFFFFFFFF;
static uint32_t COL_STAR     = 0xFFFFB020;
static uint32_t COL_GREEN    = 0xFF10B981;
static uint32_t COL_BAR_FILL = 0xFF2C7BEA;
static uint32_t COL_SEARCH   = 0xFFF0F0F3;
static uint32_t COL_TAB_OFF  = 0xFFE8E8EE;

/* ── Framebuffer ──────────────────────────────────────────────────── */

static struct {
    uint32_t width, height, pitch, size;
    uint32_t fb_id;
    uint32_t handle;
    uint32_t *fb;
    int fd;
} g_fb = {0};

/* ── App catalog ──────────────────────────────────────────────────── */

typedef struct {
    const char *id;
    const char *name;
    const char *category;   /* "games" | "applications" */
    const char *developer;
    const char *version;
    unsigned size_kb;
    float rating;
    int installed;
    int installing;
    int progress;           /* 0..100 when installing */
} App;

static App g_apps[] = {
    {"com.qeaf.angrybirds",  "Angry Birds",         "games",        "Rovio Entertainment", "2.5.0",  34, 4.8f, 0,0,0},
    {"com.qeaf.flappy",      "Flappy Bird",         "games",        "Dodreams",            "1.0.0",   8, 4.6f, 0,0,0},
    {"com.qeaf.subway",      "Subway Surfers",      "games",        "SYBO Games",          "3.12.1", 95, 4.7f, 0,0,0},
    {"com.qeaf.plants",      "Plants vs Zombies",   "games",        "EA / PopCap",         "2.0.0", 128, 4.9f, 0,0,0},
    {"com.qeaf.2048",        "2048",                "games",        "Gabriele Cirulli",    "1.0.0",   2, 4.5f, 0,0,0},
    {"com.qeaf.calculator",  "Calculator",          "applications", "Qeaf Software",       "1.2.0",   1, 4.3f, 0,0,0},
    {"com.qeaf.notes",       "Notes",               "applications", "Qeaf Software",       "1.0.4",   3, 4.4f, 0,0,0},
    {"com.qeaf.calendar",    "Calendar",            "applications", "Qeaf Software",       "1.1.0",   4, 4.5f, 0,0,0},
    {"com.qeaf.weather",     "Weather",             "applications", "Qeaf Software",       "2.0.1",   6, 4.6f, 0,0,0},
    {"com.qeaf.files",       "Files",               "applications", "Qeaf Software",       "1.3.0",   8, 4.4f, 0,0,0},
    {"com.qeaf.clock",       "Clock",               "applications", "Qeaf Software",       "1.0.0",   2, 4.2f, 0,0,0},
    {"com.qeaf.camera",      "Camera",              "applications", "Qeaf Software",       "2.1.0",  12, 4.5f, 0,0,0},
    {"com.qeaf.browser",     "Browser",             "applications", "Qeaf Software",       "3.0.0",  24, 4.7f, 0,0,0},
    {"com.qeaf.music",       "Music",               "applications", "Qeaf Software",       "1.5.0",  16, 4.6f, 0,0,0},
    {"com.qeaf.contacts",    "Contacts",            "applications", "Qeaf Software",       "1.0.2",   3, 4.1f, 0,0,0},
    {"com.qeaf.messages",    "Messages",            "applications", "Qeaf Software",       "2.2.0",  10, 4.5f, 0,0,0},
    {"com.qeaf.maps",        "Maps",                "applications", "Qeaf Software",       "4.1.0",  45, 4.3f, 0,0,0},
};

static int g_app_count = sizeof(g_apps) / sizeof(g_apps[0]);
static App *g_filtered[128];
static int g_filtered_count = 0;
static int g_filter = 0;            /* 0=all, 1=games, 2=applications */
static char g_search[64] = "";
static int g_scroll = 0;            /* pixel scroll offset */
static int g_selected = -1;         /* selected app index in g_filtered, -1 = none */
static int g_touch_fd = -1;
static int g_touch_x = 0, g_touch_y = 0;
static int g_touch_down = 0;

/* ── DRM init (same pattern as homo/platform/violet/drm_kms.c) ────── */

static int drm_init(void)
{
    struct drm_mode_card_res res;
    struct drm_mode_get_connector conn;
    struct drm_mode_get_encoder enc;
    struct drm_mode_crtc setcrtc;
    struct drm_mode_create_dumb dumb;
    struct drm_mode_fb_cmd2 fb2;
    struct drm_mode_map_dumb map;
    uint32_t connector_id;
    uint32_t crtc_ids[16], connector_ids[16], encoder_ids[16];

    g_fb.fd = open(DRIVER, O_RDWR | O_CLOEXEC);
    if (g_fb.fd < 0) {
        fprintf(stderr, "installer: cannot open %s: %s\n", DRIVER, strerror(errno));
        return -1;
    }

    /* become DRM master — msm-4.14 returns EINVAL on SETCRTC without this */
    if (ioctl(g_fb.fd, DRM_IOCTL_SET_MASTER, 0) == 0)
        fprintf(stderr, "installer: drm master acquired\n");
    else
        fprintf(stderr, "installer: SET_MASTER refused (%s)\n", strerror(errno));

    /* GETRESOURCES — count then retrieve IDs */
    memset(&res, 0, sizeof(res));
    res.crtc_id_ptr      = (uint64_t)(uintptr_t)crtc_ids;
    res.connector_id_ptr = (uint64_t)(uintptr_t)connector_ids;
    res.encoder_id_ptr   = (uint64_t)(uintptr_t)encoder_ids;
    res.count_crtcs      = 16;
    res.count_connectors = 16;
    res.count_encoders   = 16;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_GETRESOURCES, &res);

    /* GETCONNECTOR — first connector */
    memset(&conn, 0, sizeof(conn));
    conn.connector_id = connector_ids[0];
    conn.count_props  = 0;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn);
    connector_id = conn.connector_id;

    /* GETENCODER — find the crtc */
    memset(&enc, 0, sizeof(enc));
    enc.encoder_id = conn.encoder_id;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_GETENCODER, &enc);
    uint32_t crtc_id = enc.crtc_id;
    if (!crtc_id) {
        crtc_id = crtc_ids[0];
    }

    /* SETCRTC — enable the CRTC (mode_valid=1 required on msm-4.14) */
    memset(&setcrtc, 0, sizeof(setcrtc));
    setcrtc.crtc_id            = crtc_id;
    setcrtc.set_connectors_ptr = (uint64_t)(uintptr_t)&connector_id;
    setcrtc.count_connectors   = 1;
    setcrtc.mode_valid         = 1;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_SETCRTC, &setcrtc);

    /* CREATE_DUMB — 32bpp framebuffer */
    memset(&dumb, 0, sizeof(dumb));
    dumb.width  = WIDTH;
    dumb.height = HEIGHT;
    dumb.bpp    = 32;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb);
    g_fb.pitch  = dumb.pitch;
    g_fb.size   = dumb.size;
    g_fb.handle = dumb.handle;

    /* ADDFB2 */
    memset(&fb2, 0, sizeof(fb2));
    fb2.width       = WIDTH;
    fb2.height      = HEIGHT;
    fb2.pixel_format= DRM_FORMAT_XRGB8888;
    fb2.handles[0]  = dumb.handle;
    fb2.pitches[0]  = dumb.pitch;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_ADDFB2, &fb2);
    g_fb.fb_id = fb2.fb_id;

    /* MAP_DUMB */
    memset(&map, 0, sizeof(map));
    map.handle = dumb.handle;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_MAP_DUMB, &map);

    g_fb.fb = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   g_fb.fd, (off_t)map.offset);
    if (g_fb.fb == MAP_FAILED) {
        g_fb.fb = NULL;
        fprintf(stderr, "installer: mmap failed: %s\n", strerror(errno));
        return -1;
    }

    g_fb.width  = WIDTH;
    g_fb.height = HEIGHT;

    /* clear to black */
    memset(g_fb.fb, 0, g_fb.size);
    return 0;
}

/* ── Drawing primitives ───────────────────────────────────────────── */

static void fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIDTH) w = WIDTH - x;
    if (y + h > HEIGHT) h = HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    uint32_t *p = g_fb.fb + (y * g_fb.pitch / 4) + x;
    int stride = g_fb.pitch / 4;
    while (h--) {
        for (int i = 0; i < w; i++)
            p[i] = color;
        p += stride;
    }
}

static void fill_rounded(int x, int y, int w, int h, int r, uint32_t color)
{
    /* simple: fill the whole rect (rounded corners are cosmetic) */
    fill_rect(x, y, w, h, color);
}

static void draw_circle(int cx, int cy, int r, uint32_t color)
{
    for (int y = -r; y <= r; y++) {
        int w = (int)(sqrtf((float)(r * r - y * y)) + 0.5f);
        fill_rect(cx - w, cy + y, w * 2 + 1, 1, color);
    }
}

/* 5x7 monospace font for basic ASCII glyphs */
static const uint8_t F_5x7[][5] = {
    ['A'] = {0x7C,0x12,0x11,0x12,0x7C}, ['B']={0x7F,0x49,0x49,0x49,0x36},
    ['C'] = {0x3E,0x41,0x41,0x41,0x22}, ['D']={0x7F,0x41,0x41,0x22,0x1C},
    ['E'] = {0x7F,0x49,0x49,0x49,0x41}, ['F']={0x7F,0x09,0x09,0x09,0x01},
    ['G'] = {0x3E,0x41,0x51,0x21,0x5E}, ['H']={0x7F,0x08,0x08,0x08,0x7F},
    ['I'] = {0x00,0x41,0x7F,0x41,0x00}, ['J']={0x20,0x40,0x41,0x3F,0x01},
    ['K'] = {0x7F,0x08,0x14,0x22,0x41}, ['L']={0x7F,0x40,0x40,0x40,0x40},
    ['M'] = {0x7F,0x02,0x0C,0x02,0x7F}, ['N']={0x7F,0x04,0x08,0x10,0x7F},
    ['O'] = {0x3E,0x41,0x41,0x41,0x3E}, ['P']={0x7F,0x09,0x09,0x09,0x06},
    ['Q'] = {0x3E,0x41,0x51,0x21,0x5E}, ['R']={0x7F,0x09,0x19,0x29,0x46},
    ['S'] = {0x46,0x49,0x49,0x49,0x31}, ['T']={0x01,0x01,0x7F,0x01,0x01},
    ['U'] = {0x3F,0x40,0x40,0x40,0x3F}, ['V']={0x1F,0x20,0x40,0x20,0x1F},
    ['W'] = {0x3F,0x40,0x38,0x40,0x3F}, ['X']={0x63,0x14,0x08,0x14,0x63},
    ['Y'] = {0x07,0x08,0x70,0x08,0x07}, ['Z']={0x61,0x51,0x49,0x45,0x43},
    ['0'] = {0x3E,0x51,0x49,0x45,0x3E}, ['1']={0x00,0x42,0x7F,0x40,0x00},
    ['2'] = {0x42,0x61,0x51,0x49,0x46}, ['3']={0x21,0x41,0x45,0x4B,0x31},
    ['4'] = {0x18,0x14,0x12,0x7F,0x10}, ['5']={0x27,0x45,0x45,0x45,0x39},
    ['6'] = {0x3C,0x4A,0x49,0x49,0x30}, ['7']={0x01,0x71,0x09,0x05,0x03},
    ['8'] = {0x36,0x49,0x49,0x49,0x36}, ['9']={0x06,0x49,0x49,0x29,0x1E},
    [' '] = {0x00,0x00,0x00,0x00,0x00}, ['.']={0x00,0x60,0x60,0x00,0x00},
    [','] = {0x00,0x80,0x60,0x00,0x00}, [':']={0x00,0x36,0x36,0x00,0x00},
    ['-'] = {0x08,0x08,0x08,0x08,0x08}, ['/']={0x20,0x10,0x08,0x04,0x02},
    ['('] = {0x00,0x1C,0x22,0x41,0x00}, [')']={0x00,0x41,0x22,0x1C,0x00},
    ['+'] = {0x08,0x08,0x3E,0x08,0x08}, ['%']={0x62,0x64,0x08,0x13,0x23},
    ['!'] = {0x00,0x7F,0x00,0x00,0x00}, ['?']={0x02,0x01,0x51,0x09,0x06},
    ['\'']= {0x00,0x00,0x7F,0x00,0x00},
};

static void draw_char(int x, int y, char c, uint32_t color)
{
    unsigned char uc = (unsigned char)c;
    if (uc < 32 || uc > 127) uc = '?';
    if (uc >= 'a' && uc <= 'z') uc -= 32;
    const uint8_t *glyph = F_5x7[uc];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << (6 - row)))
                fill_rect(x + col, y + row, 1, 1, color);
        }
    }
}

static void draw_text(int x, int y, const char *t, uint32_t color)
{
    int px = x;
    for (int i = 0; t[i]; i++) {
        unsigned char c = (unsigned char)t[i];
        /* Vietnamese diacritics: draw the base letter, skip multi-byte UTF-8
         * continuation bytes (they don't have a 5x7 glyph) */
        if (c >= 0x80) {
            px += 4;
            continue;
        }
        draw_char(px, y, (char)c, color);
        px += 6;
    }
}

static int text_width(const char *t)
{
    int w = 0;
    for (int i = 0; t[i]; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c >= 0x80) w += 4;
        else w += 6;
    }
    return w;
}

static void draw_star(int x, int y, int filled, uint32_t color)
{
    if (filled) {
        fill_rect(x, y + 2, 8, 4, color);
        fill_rect(x + 2, y, 4, 8, color);
        fill_rect(x + 1, y + 1, 6, 6, color);
        fill_rect(x + 3, y - 1, 2, 10, color);
    } else {
        fill_rect(x, y + 2, 8, 1, color);
        fill_rect(x, y + 5, 8, 1, color);
        fill_rect(x + 1, y + 1, 6, 1, color);
        fill_rect(x + 1, y + 6, 6, 1, color);
    }
}

/* ── App filtering ────────────────────────────────────────────────── */

static int str_contains_ci(const char *hay, const char *needle)
{
    if (!needle[0]) return 1;
    int nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        int ok = 1;
        for (int i = 0; i < nl; i++) {
            char a = p[i], b = needle[i];
            if (!a) { ok = 0; break; }
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static void filter_apps(void)
{
    g_filtered_count = 0;
    for (int i = 0; i < g_app_count; i++) {
        App *a = &g_apps[i];
        int cat_ok = 1;
        if (g_filter == 1) cat_ok = (strcmp(a->category, "games") == 0);
        else if (g_filter == 2) cat_ok = (strcmp(a->category, "applications") == 0);
        int search_ok = 1;
        if (g_search[0]) {
            search_ok = str_contains_ci(a->name, g_search) ||
                        str_contains_ci(a->developer, g_search);
        }
        if (cat_ok && search_ok)
            g_filtered[g_filtered_count++] = a;
    }
}

/* ── Layout constants ─────────────────────────────────────────────── */

#define TOP_PAD     16
#define SEARCH_H    48
#define SEARCH_Y    (TOP_PAD + 8)
#define TAB_H       40
#define TAB_Y       (SEARCH_Y + SEARCH_H + 12)
#define CARD_H      140
#define CARD_GAP    12
#define CARD_W      (WIDTH - 32)
#define CARD_X      16
#define LIST_TOP    (TAB_Y + TAB_H + 12)

/* ── Draw one card ────────────────────────────────────────────────── */

static void draw_card(int x, int y, int w, int h, App *app)
{
    /* card background */
    fill_rounded(x, y, w, h, 16, COL_CARD);

    /* left accent border by category */
    uint32_t accent;
    if (strcmp(app->category, "games") == 0)       accent = 0xFF7B61FF;
    else                                            accent = 0xFF2C7BEA;
    if (app->installed) accent = COL_GREEN;
    fill_rect(x, y + 4, 4, h - 8, accent);

    /* icon circle */
    int icon_r = 24;
    int icon_cx = x + 44;
    int icon_cy = y + h / 2;
    draw_circle(icon_cx, icon_cy, icon_r, accent);
    /* first letter in icon */
    char letter = app->name[0];
    if (letter >= 'a' && letter <= 'z') letter -= 32;
    draw_char(icon_cx - 2, icon_cy - 3, letter, COL_WHITE);

    /* app name */
    draw_text(x + 80, y + 16, app->name, COL_TEXT);

    /* developer */
    draw_text(x + 80, y + 32, app->developer, COL_SUB);

    /* size + version */
    char info[128];
    if (app->installed)
        snprintf(info, sizeof(info), "Da cai  %d KB  v%s", app->size_kb, app->version);
    else
        snprintf(info, sizeof(info), "%d KB  v%s  %.1f*", app->size_kb, app->version, app->rating);
    draw_text(x + 80, y + 48, info, COL_SUB);

    /* rating stars */
    int stars = (int)app->rating;
    int sx = x + 80;
    int sy = y + 68;
    for (int i = 0; i < 5; i++)
        draw_star(sx + i * 12, sy, i < stars, COL_STAR);

    /* install button */
    int bw = 80, bh = 36;
    int bx = x + w - bw - 12;
    int by = y + (h - bh) / 2;

    if (app->installed) {
        fill_rounded(bx, by, bw, bh, 18, COL_GREEN);
        draw_text(bx + (bw - text_width("Da cai")) / 2, by + 12, "Da cai", COL_WHITE);
    } else if (app->installing) {
        fill_rounded(bx, by, bw, bh, 18, COL_BORDER);
        /* progress bar inside button */
        int pw = (bw - 8) * app->progress / 100;
        fill_rect(bx + 4, by + bh - 6, pw, 3, COL_BAR_FILL);
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", app->progress);
        draw_text(bx + (bw - text_width(pct)) / 2, by + 10, pct, COL_TEXT);
    } else {
        fill_rounded(bx, by, bw, bh, 18, COL_ACCENT);
        draw_text(bx + (bw - text_width("Cai")) / 2, by + 12, "Cai", COL_WHITE);
    }
}

/* ── Render ───────────────────────────────────────────────────────── */

static void render_home(void)
{
    /* background */
    fill_rect(0, 0, WIDTH, HEIGHT, COL_BG);

    /* status bar spacer */
    fill_rect(0, 0, WIDTH, TOP_PAD, COL_BG);

    /* search bar */
    int sx = 16, sw = WIDTH - 32;
    fill_rounded(sx, SEARCH_Y, sw, SEARCH_H, 12, COL_SEARCH);
    /* search icon (magnifier) */
    draw_circle(sx + 20, SEARCH_Y + SEARCH_H / 2, 6, COL_SUB);
    fill_rect(sx + 24, SEARCH_Y + SEARCH_H / 2 + 4, 6, 2, COL_SUB);
    /* search text or placeholder */
    if (g_search[0])
        draw_text(sx + 40, SEARCH_Y + (SEARCH_H - 7) / 2, g_search, COL_TEXT);
    else
        draw_text(sx + 40, SEARCH_Y + (SEARCH_H - 7) / 2, "Tim kiem ung dung...", COL_SUB);

    /* category tabs */
    const char *tabs[] = {"Tat ca", "Game", "Ung dung"};
    int tab_w = (WIDTH - 32) / 3;
    for (int i = 0; i < 3; i++) {
        int tx = 16 + i * tab_w;
        if (i == g_filter) {
            fill_rounded(tx, TAB_Y, tab_w - 4, TAB_H, 20, COL_ACCENT);
            draw_text(tx + (tab_w - 4 - text_width(tabs[i])) / 2,
                      TAB_Y + (TAB_H - 7) / 2, tabs[i], COL_WHITE);
        } else {
            fill_rounded(tx, TAB_Y, tab_w - 4, TAB_H, 20, COL_TAB_OFF);
            draw_text(tx + (tab_w - 4 - text_width(tabs[i])) / 2,
                      TAB_Y + (TAB_H - 7) / 2, tabs[i], COL_SUB);
        }
    }

    /* app list */
    int cy = LIST_TOP - g_scroll;
    for (int i = 0; i < g_filtered_count; i++) {
        int y = cy + i * (CARD_H + CARD_GAP);
        if (y + CARD_H < 0) continue;
        if (y > HEIGHT) break;
        draw_card(CARD_X, y, CARD_W, CARD_H, g_filtered[i]);
    }

    /* bottom safe area */
    fill_rect(0, HEIGHT - 8, WIDTH, 8, COL_BG);
}

/* ── Touch input ──────────────────────────────────────────────────── */

static int touch_init(void)
{
    g_touch_fd = open(TOUCH, O_RDONLY | O_NONBLOCK);
    if (g_touch_fd < 0) {
        fprintf(stderr, "installer: cannot open %s: %s\n", TOUCH, strerror(errno));
        return -1;
    }
    return 0;
}

static void touch_poll(void)
{
    struct input_event ev;
    while (read(g_touch_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        switch (ev.type) {
        case EV_ABS:
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)
                g_touch_x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y)
                g_touch_y = ev.value;
            break;
        case EV_KEY:
            if (ev.code == BTN_TOUCH)
                g_touch_down = ev.value;
            break;
        case EV_SYN:
            if (ev.code == SYN_REPORT && g_touch_down) {
                /* tap detected — check which card was tapped */
                int cy = LIST_TOP - g_scroll;
                for (int i = 0; i < g_filtered_count; i++) {
                    int card_y = cy + i * (CARD_H + CARD_GAP);
                    if (g_touch_y >= card_y && g_touch_y < card_y + CARD_H &&
                        g_touch_x >= CARD_X && g_touch_x < CARD_X + CARD_W) {
                        /* tapped card i — toggle install */
                        App *a = g_filtered[i];
                        if (!a->installed && !a->installing) {
                            a->installing = 1;
                            a->progress = 0;
                        }
                        break;
                    }
                }
                /* check tab taps */
                if (g_touch_y >= TAB_Y && g_touch_y < TAB_Y + TAB_H) {
                    int tab_w = (WIDTH - 32) / 3;
                    int tab = (g_touch_x - 16) / tab_w;
                    if (tab >= 0 && tab < 3) g_filter = tab;
                    filter_apps();
                    g_scroll = 0;
                }
            }
            break;
        }
    }
}

/* ── Simulate install progress ────────────────────────────────────── */

static void update_installs(void)
{
    for (int i = 0; i < g_app_count; i++) {
        App *a = &g_apps[i];
        if (a->installing) {
            a->progress += 2;
            if (a->progress >= 100) {
                a->progress = 100;
                a->installing = 0;
                a->installed = 1;
            }
        }
    }
}

/* ── Main loop ────────────────────────────────────────────────────── */

int main(void)
{
    if (drm_init() < 0) return 1;
    touch_init();  /* non-fatal if it fails */
    filter_apps();

    for (;;) {
        touch_poll();
        update_installs();
        render_home();
        usleep(16000);  /* ~60fps */
    }

    return 0;
}
