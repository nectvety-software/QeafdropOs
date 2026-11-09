#include "ios_metrics.h"

/* Rounded integer ratio: 540x60/390 = 83, not 83.07 truncated to 83 anyway but
 * the rounding keeps 8.2% W gaps from drifting a pixel between sizes. */
static int scaled(int value, int numerator, int denominator)
{
    if(value <= 0 || denominator <= 0) return 0;
    return (value * numerator + denominator / 2) / denominator;
}

static void portrait_metrics(const int width, const int height, ios_metrics_t *m)
{
    m->columns = 4;
    m->icon = scaled(width, 60, 390);      /* 15.4% W */
    m->gap_x = scaled(width, 32, 390);     /* 8.2% W */
    m->margin_x = (width - m->columns * m->icon -
                   (m->columns - 1) * m->gap_x) / 2;
    if(m->margin_x < 0) m->margin_x = 0;

    m->label_font = scaled(width, 11, 390);
    m->label_gap = scaled(width, 4, 390);
    m->label_width = m->icon + scaled(width, 30, 390);

    m->first_row_top = scaled(height, 79, 844);  /* 9.4% H */
    m->row_pitch = scaled(height, 107, 844);     /* 12.7% H */
    if(m->row_pitch < m->icon) m->row_pitch = m->icon;

    m->dock_height = scaled(height, 82, 844);
    m->dock_top = height - scaled(height, 20, 844) - m->dock_height;
    m->dock_margin_x = scaled(width, 8, 390);
    m->dock_radius = scaled(m->dock_height, 39, 100);

    m->rows_capacity = m->row_pitch > 0 ?
        (m->dock_top - m->first_row_top) / m->row_pitch : 0;
    if(m->rows_capacity < 1) m->rows_capacity = 1;

    m->home_bar_w = scaled(width, 134, 390);  /* 34.4% W */
    m->home_bar_h = scaled(height, 5, 844);
    if(m->home_bar_h < 3) m->home_bar_h = 3;
    m->home_bar_y = height - scaled(height, 8, 844) - m->home_bar_h;

    m->status_height = scaled(height, 33, 844);
    m->status_font = scaled(width, 16, 390);
    m->status_pad_top = scaled(width, 10, 390);
    m->status_pad_left = scaled(width, 20, 390);
    m->status_wifi = scaled(width, 23, 390);
    m->status_battery = scaled(width, 32, 390);
}

static void landscape_metrics(const int width, const int height, ios_metrics_t *m)
{
    /* The 800x480 iPad-style grid the apps were drawn for: unchanged. */
    m->columns = 6;
    m->icon = 64;
    m->gap_x = 69;
    m->margin_x = (width - m->columns * m->icon -
                   (m->columns - 1) * m->gap_x) / 2;
    if(m->margin_x < 0) m->margin_x = 0;

    m->label_font = 14;
    m->label_gap = 6;
    m->label_width = 132;

    m->first_row_top = 56;
    m->row_pitch = 0;   /* rows spread over the free space */
    m->rows_capacity = 0;

    m->dock_height = 86;
    m->dock_top = height - m->dock_height - 8;
    m->dock_margin_x = 10;
    m->dock_radius = 25;

    m->home_bar_w = 140;
    m->home_bar_h = 5;
    m->home_bar_y = height - 10;

    m->status_height = 32;
    m->status_font = 18;
    m->status_pad_top = 6;
    m->status_pad_left = 16;
    m->status_wifi = 26;
    m->status_battery = 36;
}

void ios_metrics(const int width, const int height, ios_metrics_t *out)
{
    if(out == 0) return;
    *out = (ios_metrics_t){0};
    out->width = width > 0 ? width : 540;
    out->height = height > 0 ? height : 1170;
    out->portrait = out->height > out->width;
    if(out->portrait) portrait_metrics(out->width, out->height, out);
    else landscape_metrics(out->width, out->height, out);
}

int ios_column_center(const ios_metrics_t *m, int column)
{
    if(m == 0 || column < 0) return 0;
    return m->margin_x + m->icon / 2 + column * (m->icon + m->gap_x);
}
