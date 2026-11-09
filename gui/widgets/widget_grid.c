/* SPDX-License-Identifier: MIT */
/* gui/widgets/widget_grid.c - resizable home widgets, integer geometry. */

#include "gui/widgets/widget_grid.h"
#include "apps/bootui/bootui.h"

#include <string.h>

static struct home_widget g_w[WG_MAX];
static int g_n;
static int g_next_id = 1;

static const char *const NAMES[WG_COUNT] = {
    "Clock", "Battery", "Weather", "Notes"
};

const char *widget_kind_name(int kind) {
    if (kind < 0 || kind >= WG_COUNT) return "unknown";
    return NAMES[kind];
}

void widget_grid_init(void) {
    memset(g_w, 0, sizeof(g_w));
    g_n = 0;
    g_next_id = 1;
}

static int valid_rect(int col, int row, int w, int h) {
    return col >= 0 && row >= 0 && w >= 1 && h >= 1 &&
           col + w <= WG_COLS && row + h <= WG_ROWS;
}

/* `skip` is the index being moved/resized, so a widget never collides with
 * its own old position. */
static int overlaps(int skip, int col, int row, int w, int h) {
    for (int i = 0; i < g_n; i++) {
        if (i == skip) continue;
        if (col < g_w[i].col + g_w[i].w_cells &&
            col + w > g_w[i].col &&
            row < g_w[i].row + g_w[i].h_cells &&
            row + h > g_w[i].row) return 1;
    }
    return 0;
}

static int find_id(int id) {
    for (int i = 0; i < g_n; i++) if (g_w[i].id == id) return i;
    return -1;
}

int widget_grid_add(int kind, int col, int row, int w, int h) {
    struct home_widget *p;
    if (kind < 0 || kind >= WG_COUNT || g_n >= WG_MAX) return -1;
    if (!valid_rect(col, row, w, h) || overlaps(-1, col, row, w, h)) return -1;
    p = &g_w[g_n++];
    p->id = g_next_id++;
    p->kind = kind;
    p->col = col; p->row = row;
    p->w_cells = w; p->h_cells = h;
    return p->id;
}

int widget_grid_resize(int id, int w, int h) {
    int i = find_id(id);
    if (i < 0) return -1;
    if (!valid_rect(g_w[i].col, g_w[i].row, w, h) ||
        overlaps(i, g_w[i].col, g_w[i].row, w, h)) return -1;
    g_w[i].w_cells = w;
    g_w[i].h_cells = h;
    return 0;
}

int widget_grid_move(int id, int col, int row) {
    int i = find_id(id);
    if (i < 0) return -1;
    if (!valid_rect(col, row, g_w[i].w_cells, g_w[i].h_cells) ||
        overlaps(i, col, row, g_w[i].w_cells, g_w[i].h_cells)) return -1;
    g_w[i].col = col;
    g_w[i].row = row;
    return 0;
}

int widget_grid_remove(int id) {
    int i = find_id(id);
    if (i < 0) return -1;
    memmove(&g_w[i], &g_w[i + 1],
            (size_t)(g_n - i - 1) * sizeof(g_w[0]));
    g_n--;
    return 0;
}

int widget_grid_count(void) { return g_n; }

int widget_grid_get(int id, struct home_widget *out) {
    int i = find_id(id);
    if (i < 0 || !out) return -1;
    *out = g_w[i];
    return 0;
}

void widget_grid_rect(int id, int *x, int *y, int *w, int *h) {
    int i = find_id(id);
    if (i < 0) {
        if (x) *x = 0; if (y) *y = 0;
        if (w) *w = 0; if (h) *h = 0;
        return;
    }
    if (x) *x = WG_X0 + g_w[i].col * WG_CELL + WG_GAP / 2;
    if (y) *y = WG_Y0 + g_w[i].row * WG_CELL + WG_GAP / 2;
    if (w) *w = g_w[i].w_cells * WG_CELL - WG_GAP;
    if (h) *h = g_w[i].h_cells * WG_CELL - WG_GAP;
}

int widget_grid_hit(int vx, int vy) {
    for (int i = g_n - 1; i >= 0; i--) {
        int x, y, w, h;
        widget_grid_rect(g_w[i].id, &x, &y, &w, &h);
        if (vx >= x && vx < x + w && vy >= y && vy < y + h) return g_w[i].id;
    }
    return -1;
}

void widget_grid_draw(void) {
    for (int i = 0; i < g_n; i++) {
        int x, y, w, h;
        widget_grid_rect(g_w[i].id, &x, &y, &w, &h);
        bui_round(x, y, w, h, 10, BUI_SURFACE, BUI_BORDER);
        bui_text(widget_kind_name(g_w[i].kind), x + 8, y + 8, 1, BUI_TEAL);
        if (w >= 2 * WG_CELL - WG_GAP) {
            /* A wider widget shows a second line: the layout really depends
             * on the cell count, not only on the card's border. */
            bui_text("resizable", x + 8, y + 24, 1, BUI_DIM);
        }
        /* resize handle in the bottom-right corner */
        bui_fill(x + w - 10, y + h - 10, 7, 7, BUI_TEAL);
    }
}
