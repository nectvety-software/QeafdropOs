/* SPDX-License-Identifier: MIT */
/* gui/widgets/widget_grid.h - resizable home-screen widgets.
 *
 * A 4x6 cell grid in the 360x780 virtual space. A widget owns a rectangle of
 * whole cells; move and resize both refuse overlaps and out-of-grid values,
 * so the home screen can never end up with two widgets inked on top of each
 * other. All geometry is integer and shared by draw + hit test.
 */

#ifndef UMOS_WIDGET_GRID_H
#define UMOS_WIDGET_GRID_H

#include <stdint.h>

#define WG_COLS      4
#define WG_ROWS      6
#define WG_CELL      84
#define WG_GAP       4
#define WG_X0        8
#define WG_Y0      120
#define WG_MAX       8

enum wg_kind {
    WG_CLOCK = 0,
    WG_BATTERY,
    WG_WEATHER,
    WG_NOTES,
    WG_COUNT
};

struct home_widget {
    int id;
    int kind;
    int col, row;
    int w_cells, h_cells;
};

void widget_grid_init(void);
/* Returns the new id, or -1 when the rect is invalid or overlaps. */
int  widget_grid_add(int kind, int col, int row, int w, int h);
int  widget_grid_resize(int id, int w, int h);
int  widget_grid_move(int id, int col, int row);
int  widget_grid_remove(int id);
/* Virtual coordinates -> widget id, or -1. */
int  widget_grid_hit(int vx, int vy);
int  widget_grid_count(void);
int  widget_grid_get(int id, struct home_widget *out);
void widget_grid_rect(int id, int *x, int *y, int *w, int *h);
void widget_grid_draw(void);
const char *widget_kind_name(int kind);

#endif
