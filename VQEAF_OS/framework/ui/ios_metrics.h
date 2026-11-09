#ifndef HACHIMI_IOS_METRICS_H
#define HACHIMI_IOS_METRICS_H

/* Pure geometry for the Home screen. The portrait grid follows an iPhone
 * Home screen measured in points (390x844) and expressed as fractions of the
 * canvas, so the same proportions hold at 540x1170 and at any other portrait
 * size. The landscape branch returns the original 800x480 iPad-style numbers.
 *
 * No LVGL dependency: tests/ios_metrics_test.c runs this on the host. */

typedef struct {
    int width;
    int height;
    int portrait;

    int columns;
    int icon;             /* side of the icon square */
    int margin_x;         /* left edge of column 0 */
    int gap_x;            /* space between two icon squares */
    int label_font;       /* px */
    int label_gap;        /* icon bottom -> label top */
    int label_width;

    int first_row_top;
    int row_pitch;        /* 0 = spread the rows over the free space (landscape) */
    int rows_capacity;

    int dock_height;
    int dock_top;
    int dock_margin_x;    /* left edge of the dock panel */
    int dock_radius;

    int home_bar_w;
    int home_bar_h;
    int home_bar_y;

    int status_height;
    int status_font;
    int status_pad_top;
    int status_pad_left;
    int status_wifi;
    int status_battery;
} ios_metrics_t;

void ios_metrics(const int width, const int height, ios_metrics_t *out);

/* Centre of a grid column, in pixels from the left edge. */
int ios_column_center(const ios_metrics_t *m, int column);

#endif
