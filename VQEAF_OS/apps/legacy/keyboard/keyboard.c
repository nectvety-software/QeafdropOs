/* SPDX-License-Identifier: MIT */
/* apps/keyboard/keyboard.c - procedural "Blueprint" keyboard, 360x780.
 *
 * Reference: a light grid sheet, one rounded near-black plate, four rows of
 * silver keys with 1..0 superscripts, dark Shift / ?123 / emoji / Return,
 * and two orange keys (backspace + the long spacebar).
 *
 * Everything is integer RGB565 ink. No assets, no floats, no libm.
 */

#include "apps/keyboard/keyboard.h"

#include "apps/bootui/bootui.h"
#include "gui/gfx/gfx.h"
#include "hal/include/display.h"

#include <stddef.h>
#include <string.h>

/* ---- palette -------------------------------------------------------------- */
#define KB_PAPER    BUI_COL(0xF2, 0xF6, 0xFC)
#define KB_GRID     BUI_COL(0xDF, 0xE8, 0xF6)
#define KB_GRID_HI  BUI_COL(0xC9, 0xD8, 0xEE)
#define KB_NAVY     BUI_COL(0x22, 0x31, 0x4F)
#define KB_NAVY_DIM BUI_COL(0x6B, 0x7C, 0x9C)
#define KB_BADGE    BUI_COL(0x3D, 0x5A, 0xFE)
#define KB_PLATE    BUI_COL(0x0B, 0x0F, 0x15)
#define KB_PLATE_ED BUI_COL(0x30, 0x39, 0x47)
#define KB_KEY_TOP  BUI_COL(0xFB, 0xFC, 0xFE)
#define KB_KEY_BOT  BUI_COL(0xC2, 0xCA, 0xD4)
#define KB_KEY_ED   BUI_COL(0x94, 0x9F, 0xAE)
#define KB_KEY_INK  BUI_COL(0x38, 0x43, 0x54)
#define KB_KEY_SUB  BUI_COL(0x83, 0x8D, 0x9D)
#define KB_DARK     BUI_COL(0x26, 0x2D, 0x38)
#define KB_DARK_ED  BUI_COL(0x49, 0x53, 0x62)
#define KB_LIGHT_INK BUI_COL(0xF2, 0xF5, 0xF9)
#define KB_ORANGE   BUI_COL(0xE8, 0x7A, 0x22)
#define KB_ORANGE_HI BUI_COL(0xF8, 0xA0, 0x52)
#define KB_ORANGE_ED BUI_COL(0xB0, 0x55, 0x10)

/* ---- plate geometry in the 360x780 design -------------------------------- */
#define PLATE_X  16
#define PLATE_Y 126
#define PLATE_W 328
#define PLATE_H 246
#define ROW_Y0  136
#define ROW_STEP 58
#define KEY_H    52
#define N_ROWS   4
#define MAX_KEYS 40

enum { K_LIGHT = 0, K_DARK, K_ORANGE };

/* Codes above 127 are actions, not characters. */
enum { C_SHIFT = 128, C_BACK, C_SYMBOLS, C_EMOJI, C_SPACE, C_RETURN };

struct kb_key {
    int x, y, w, h;
    char label[6];
    char sub[3];
    int kind;
    int code;
};

static struct kb_key g_keys[MAX_KEYS];
static int g_nkeys;
static char g_text[48];
static int g_shift;
static int g_symbols;
static int g_visible;
static int g_ox, g_oy;
static enum keyboard_action g_act;
static int g_arg;

/* Rotation buttons live below the plate. */
#define ROT_Y 636
#define ROT_H 34
static const int g_rot_x[5] = { 16, 84, 152, 220, 288 };
static const int g_rot_w[5] = { 60, 60, 60, 60, 56 };
static const char *const g_rot_label[5] = { "0", "90", "180", "270", "AUTO" };
static const int g_rot_value[5] = { 0, 1, 2, 3, -2 };

/* ---- small helpers -------------------------------------------------------- */
static void X(int *v) { *v += g_ox; }
static void Y(int *v) { *v += g_oy; }

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void fill(int x, int y, int w, int h, struct umos_color c) {
    X(&x); Y(&y);
    bui_fill(x, y, w, h, c);
}
static void hline(int x, int y, int w, struct umos_color c) {
    X(&x); Y(&y);
    bui_hline(x, y, w, c);
}
static void round_rect(int x, int y, int w, int h, int r,
                       struct umos_color f, struct umos_color b) {
    X(&x); Y(&y);
    bui_round(x, y, w, h, r, f, b);
}
static void gradient(int x, int y, int w, int h,
                     struct umos_color a, struct umos_color b) {
    X(&x); Y(&y);
    bui_gradient(x, y, w, h, a, b);
}
static void text(const char *s, int x, int y, int scale, struct umos_color c) {
    X(&x); Y(&y);
    bui_text(s, x, y, scale, c);
}
static void text_c(const char *s, int cx, int y, int scale,
                   struct umos_color c) {
    X(&cx); Y(&y);
    bui_text_center(s, cx, y, scale, c);
}

/* ---- procedural glyphs (16x16 virtual boxes) ----------------------------- */
static void glyph_shift(int x, int y) {
    int cx = x + 8;
    for (int i = 0; i < 6; i++) {
        fill(cx - i, y + 3 + i, 1, 1, KB_LIGHT_INK);
        fill(cx + i, y + 3 + i, 1, 1, KB_LIGHT_INK);
    }
    fill(cx - 6, y + 9, 13, 1, KB_LIGHT_INK);
    fill(cx - 3, y + 9, 7, 5, KB_LIGHT_INK);
    fill(cx - 2, y + 10, 5, 4, KB_DARK);
    fill(x + 14, y + 1, 2, 2, KB_LIGHT_INK);   /* the little dot in the poster */
}
static void glyph_back(int x, int y) {
    int i;
    for (i = 0; i < 5; i++) {
        fill(x + 2 + i, y + 8 - i, 1, 1, KB_LIGHT_INK);
        fill(x + 2 + i, y + 8 + i, 1, 1, KB_LIGHT_INK);
    }
    fill(x + 2, y + 8 - 5, 1, 11, KB_LIGHT_INK);
    fill(x + 2, y + 3, 11, 1, KB_LIGHT_INK);
    fill(x + 13, y + 4, 1, 9, KB_LIGHT_INK);
    fill(x + 2, y + 13, 11, 1, KB_LIGHT_INK);
    for (i = 0; i < 5; i++) {
        fill(x + 7 + i, y + 6 + i, 1, 1, KB_LIGHT_INK);
        fill(x + 11 - i, y + 6 + i, 1, 1, KB_LIGHT_INK);
    }
}
static void glyph_smile(int x, int y) {
    int cx = x + 8, cy = y + 8;
    /* Boxed circle, eyes and a smile - deterministic integer ink. */
    fill(cx - 4, cy - 5, 9, 1, KB_LIGHT_INK);
    fill(cx - 4, cy + 5, 9, 1, KB_LIGHT_INK);
    fill(cx - 6, cy - 3, 1, 7, KB_LIGHT_INK);
    fill(cx + 5, cy - 3, 1, 7, KB_LIGHT_INK);
    fill(cx - 5, cy - 4, 1, 1, KB_LIGHT_INK);
    fill(cx + 4, cy - 4, 1, 1, KB_LIGHT_INK);
    fill(cx - 5, cy + 4, 1, 1, KB_LIGHT_INK);
    fill(cx + 4, cy + 4, 1, 1, KB_LIGHT_INK);
    fill(cx - 3, cy - 2, 2, 2, KB_LIGHT_INK);
    fill(cx + 2, cy - 2, 2, 2, KB_LIGHT_INK);
    fill(cx - 2, cy + 2, 5, 1, KB_LIGHT_INK);
    fill(cx - 3, cy + 1, 1, 1, KB_LIGHT_INK);
    fill(cx + 3, cy + 1, 1, 1, KB_LIGHT_INK);
}
static void glyph_return(int x, int y) {
    fill(x + 3, y + 3, 1, 8, KB_LIGHT_INK);
    fill(x + 3, y + 11, 9, 1, KB_LIGHT_INK);
    for (int i = 0; i < 4; i++) {
        fill(x + 12 - i, y + 7 + i, 1, 1, KB_LIGHT_INK);
        fill(x + 12 - i, y + 15 - i, 1, 1, KB_LIGHT_INK);
    }
    fill(x + 3, y + 3, 2, 1, KB_LIGHT_INK);
    fill(x + 3, y + 4, 2, 1, KB_LIGHT_INK);
}

/* ---- layout --------------------------------------------------------------- */
static void add_key(int row, int x, int w, const char *label,
                    const char *sub, int kind, int code) {
    struct kb_key *k;
    if (g_nkeys >= MAX_KEYS) return;
    k = &g_keys[g_nkeys++];
    k->x = x; k->y = ROW_Y0 + row * ROW_STEP; k->w = w; k->h = KEY_H;
    memset(k->label, 0, sizeof(k->label));
    memset(k->sub, 0, sizeof(k->sub));
    if (label && label[0]) {
        int n = slen(label);
        if (n > (int)sizeof(k->label) - 1) n = sizeof(k->label) - 1;
        memcpy(k->label, label, (size_t)n);
    }
    if (sub && sub[0]) memcpy(k->sub, sub, slen(sub) > 2 ? 2 : (size_t)slen(sub));
    k->kind = kind;
    k->code = code;
}

static void build_layout(void) {
    static const char *const L1 = "QWERTYUIOP";
    static const char *const L2 = "ASDFGHJKL";
    static const char *const L3 = "ZXCVBNM";
    static const char *const S1 = "1234567890";
    static const char *const S2 = "-/:;()$&@";
    static const char *const S3 = ".,?!'\"_";
    static const char *const NUMSUB = "1234567890";
    char one[2];
    int i, x;

    g_nkeys = 0;
    one[1] = '\0';
    /* Row 1: ten silver keys with 1..0 superscripts. */
    x = 27;
    for (i = 0; i < 10; i++) {
        one[0] = g_symbols ? S1[i] :
                 (g_shift ? L1[i] : (char)(L1[i] + 32));
        add_key(0, x, 27, one, "", K_LIGHT, one[0]);
        x += 31;
    }
    /* Row 2: nine keys, indented. */
    x = 42;
    for (i = 0; i < 9; i++) {
        one[0] = g_symbols ? S2[i] : (g_shift ? L2[i] : (char)(L2[i] + 32));
        add_key(1, x, 27, one, "", K_LIGHT, one[0]);
        x += 31;
    }
    /* Row 3: dark Shift, seven letters, orange Backspace. */
    add_key(2, 24, 40, "", "", K_DARK, C_SHIFT);
    x = 70;
    for (i = 0; i < 7; i++) {
        one[0] = g_symbols ? S3[i] : (g_shift ? L3[i] : (char)(L3[i] + 32));
        add_key(2, x, 27, one, "", K_LIGHT, one[0]);
        x += 31;
    }
    add_key(2, 290, 46, "", "", K_ORANGE, C_BACK);
    /* Row 4: ?123, comma, emoji, orange spacebar, period, dark Return. */
    add_key(3, 24, 40, g_symbols ? "ABC" : "?123", "", K_DARK, C_SYMBOLS);
    one[0] = ','; add_key(3, 68, 30, one, "", K_LIGHT, ',');
    add_key(3, 102, 32, "", "", K_DARK, C_EMOJI);
    add_key(3, 138, 120, "", "", K_ORANGE, C_SPACE);
    one[0] = '.'; add_key(3, 262, 30, one, "", K_LIGHT, '.');
    add_key(3, 296, 40, "", "", K_DARK, C_RETURN);

    /* Superscripts 1..0 on the ten top-row keys, like the poster. */
    if (!g_symbols) {
        for (i = 0; i < 10; i++) {
            g_keys[i].sub[0] = NUMSUB[i];
            g_keys[i].sub[1] = '\0';
        }
    }
}

/* ---- drawing -------------------------------------------------------------- */
static void draw_grid(void) {
    int x, y;
    /* The paper is the base layer: without it the dark overlay underneath
     * shows through and the grid reads as white lines on black. It covers
     * the whole 360x780 design; the helpers add the keyboard's own centring
     * origin, so the sheet travels with the plate on a wide surface. */
    fill(0, 0, BUI_V_W, BUI_V_H, KB_PAPER);
    for (x = 0; x < BUI_V_W; x += 12)
        fill(x, 0, 1, BUI_V_H, (x % 60 == 0) ? KB_GRID_HI : KB_GRID);
    for (y = 0; y < BUI_V_H; y += 12)
        hline(0, y, BUI_V_W, (y % 60 == 0) ? KB_GRID_HI : KB_GRID);
}

static void draw_header(void) {
    int bx = 24, by = 30;
    round_rect(bx, by, 52, 52, 12, KB_BADGE, KB_BADGE);
    /* tiny blueprint glyph in the badge */
    fill(bx + 12, by + 14, 10, 10, KB_LIGHT_INK);
    fill(bx + 14, by + 16, 6, 6, KB_BADGE);
    fill(bx + 28, by + 14, 10, 10, KB_LIGHT_INK);
    fill(bx + 31, by + 17, 4, 4, KB_BADGE);
    fill(bx + 12, by + 30, 26, 3, KB_LIGHT_INK);
    fill(bx + 20, by + 33, 3, 8, KB_LIGHT_INK);
    text("VQEAF", 88, 34, 3, KB_NAVY);
    text("Blueprint", 88, 60, 2, KB_NAVY_DIM);
    {
        char line[48];
        int n = 0;
        const char *o = display_orientation_name(display_orientation());
        line[0] = '\0';
        while (o[n] && n < 20) { line[n] = o[n]; n++; }
        line[n] = '\0';
        text(line, 232, 40, 1, KB_NAVY_DIM);
        text(display_auto_rotate() ? "AUTO" : "LOCKED", 232, 54, 1, KB_NAVY_DIM);
    }
}

static void draw_key(const struct kb_key *k) {
    if (k->kind == K_LIGHT) {
        round_rect(k->x, k->y, k->w, k->h, 7, KB_KEY_TOP, KB_KEY_ED);
        gradient(k->x + 2, k->y + 2, k->w - 4, k->h - 5,
                 KB_KEY_TOP, KB_KEY_BOT);
        hline(k->x + 3, k->y + k->h - 4, k->w - 6, KB_KEY_BOT);
        if (k->sub[0])
            text(k->sub, k->x + k->w - 9, k->y + 4, 1, KB_KEY_SUB);
        if (k->label[0])
            text_c(k->label, k->x + k->w / 2, k->y + 18, 2, KB_KEY_INK);
    } else if (k->kind == K_ORANGE) {
        round_rect(k->x, k->y, k->w, k->h, 7, KB_ORANGE, KB_ORANGE_ED);
        gradient(k->x + 2, k->y + 2, k->w - 4, k->h - 5,
                 KB_ORANGE_HI, KB_ORANGE);
        if (k->code == C_SPACE) {
            /* the thin inner outline from the reference */
            round_rect(k->x + 4, k->y + 5, k->w - 8, k->h - 10, 4,
                       KB_ORANGE, KB_ORANGE_HI);
        } else {
            glyph_back(k->x + k->w / 2 - 8, k->y + k->h / 2 - 8);
        }
    } else {
        round_rect(k->x, k->y, k->w, k->h, 7, KB_DARK, KB_DARK_ED);
        if (k->code == C_SHIFT)
            glyph_shift(k->x + k->w / 2 - 8, k->y + k->h / 2 - 8);
        else if (k->code == C_EMOJI)
            glyph_smile(k->x + k->w / 2 - 8, k->y + k->h / 2 - 8);
        else if (k->code == C_RETURN)
            glyph_return(k->x + k->w / 2 - 8, k->y + k->h / 2 - 8);
        else if (k->label[0])
            text_c(k->label, k->x + k->w / 2, k->y + 20, 1, KB_LIGHT_INK);
    }
}

static void draw_preview(void) {
    const char *t = g_text[0] ? g_text : "type something";
    char count[12];
    int n = slen(g_text), i = 0;

    text(t, 24, 404, 2, KB_NAVY);
    text("@vqeaf blueprint", 24, 434, 1, KB_NAVY_DIM);
    do {
        int v = n;
        char tmp[8];
        int k = 0;
        do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (k) { count[i++] = tmp[--k]; }
    } while (0);
    count[i] = '\0';
    text(count, 24, 466, 4, KB_NAVY);
    text("Search in VQEAF Blueprint", 24, 530, 1, KB_NAVY_DIM);
    text("Screen rotation", 24, 610, 1, KB_NAVY_DIM);
}

static void draw_rotation_buttons(void) {
    int i;
    for (i = 0; i < 5; i++) {
        int on = (g_rot_value[i] == -2)
                 ? display_auto_rotate()
                 : ((int)display_orientation() == g_rot_value[i] &&
                    !display_auto_rotate());
        round_rect(g_rot_x[i], ROT_Y, g_rot_w[i], ROT_H, 8,
                   on ? KB_ORANGE : KB_NAVY,
                   on ? KB_ORANGE_ED : KB_NAVY);
        text_c(g_rot_label[i], g_rot_x[i] + g_rot_w[i] / 2,
               ROT_Y + 12, 1, KB_LIGHT_INK);
    }
    text("Tap a key to type. Shift, ?123, emoji work.", 24, 690, 1, KB_NAVY_DIM);
    text("BACK closes the keyboard.", 24, 706, 1, KB_NAVY_DIM);
}

/* ---- public --------------------------------------------------------------- */

void keyboard_init(void) {
    g_visible = 0;
    g_act = KEYBOARD_ACT_NONE;
    g_arg = 0;
    g_shift = 0;
    g_symbols = 0;
    g_text[0] = '\0';
    g_ox = g_oy = 0;
    build_layout();
}

void keyboard_open(void) {
    g_visible = 1;
    g_act = KEYBOARD_ACT_NONE;
    build_layout();
}

void keyboard_close(void) { g_visible = 0; }

int keyboard_visible(void) { return g_visible; }

void keyboard_origin(int *ox, int *oy) {
    if (ox) *ox = g_ox;
    if (oy) *oy = g_oy;
}

void keyboard_draw(void) {
    int i;
    if (!g_visible) return;
    bui_begin_overlay();
    bui_view_legacy();

    /* The plate keeps its 360x780 design at the fit scale; centre it here
     * because the legacy view deliberately carries no origin. */
    g_ox = (int)(gfx_logical_width() / (uint32_t)bui_scale() - 360) / 2;
    g_oy = (int)(gfx_logical_height() / (uint32_t)bui_scale() - 780) / 2;
    if (g_ox < 0) g_ox = 0;
    if (g_oy < 0) g_oy = 0;
    draw_grid();                 /* grid is drawn in surface coords, not offset */
    draw_header();
    round_rect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, 14,
               KB_PLATE, KB_PLATE_ED);
    for (i = 0; i < g_nkeys; i++) draw_key(&g_keys[i]);
    draw_preview();
    draw_rotation_buttons();
    bui_end();
}

static int in_rect(int vx, int vy, int x, int y, int w, int h) {
    return vx >= x && vx < x + w && vy >= y && vy < y + h;
}

static void type_char(char c) {
    int n = slen(g_text);
    if (n < (int)sizeof(g_text) - 1) { g_text[n] = c; g_text[n + 1] = '\0'; }
}

static void backspace(void) {
    int n = slen(g_text);
    if (n > 0) g_text[n - 1] = '\0';
}

int keyboard_event(uint32_t type, uint32_t d1, uint32_t d2) {
    int vx, vy, i;

    if (!g_visible) return 0;
    if (type == KEYBOARD_EV_BACK) { keyboard_close(); return 1; }
    if (type != KEYBOARD_EV_TOUCH) return 0;
    bui_map_touch((int)d1, (int)d2, &vx, &vy);
    vx -= g_ox;
    vy -= g_oy;

    for (i = 0; i < 5; i++) {
        if (!in_rect(vx, vy, g_rot_x[i], ROT_Y, g_rot_w[i], ROT_H)) continue;
        g_act = KEYBOARD_ACT_ROTATE;
        g_arg = g_rot_value[i];
        return 1;
    }
    for (i = 0; i < g_nkeys; i++) {
        const struct kb_key *k = &g_keys[i];
        if (!in_rect(vx, vy, k->x, k->y, k->w, k->h)) continue;
        switch (k->code) {
        case C_SHIFT:   g_shift = !g_shift; build_layout(); return 1;
        case C_SYMBOLS: g_symbols = !g_symbols; build_layout(); return 1;
        case C_BACK:    backspace(); return 1;
        case C_SPACE:   type_char(' '); return 1;
        case C_RETURN:  type_char('\n'); return 1;
        case C_EMOJI:   type_char(':'); type_char(')'); return 1;
        default:        type_char((char)k->code); return 1;
        }
    }
    return 1;
}

enum keyboard_action keyboard_take_action(int *arg) {
    enum keyboard_action a = g_act;
    if (arg) *arg = g_arg;
    g_act = KEYBOARD_ACT_NONE;
    g_arg = 0;
    return a;
}

const char *keyboard_text(void) { return g_text; }
int keyboard_key_count(void) { return g_nkeys; }
int keyboard_symbols(void) { return g_symbols; }
int keyboard_shift(void) { return g_shift; }
