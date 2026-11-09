/* Host gate for the portrait Home geometry (homo/framework/ui/ios_metrics.c).
 *
 * The reference is an iPhone Home screen measured on a 390x844 pt canvas: a
 * 4-column grid, 60 pt icons, 32 pt gaps, 107 pt row pitch, an 82 pt dock. The
 * shipping canvas is 540x1170, so every number is a fraction and this
 * gate pins the rounded result at both sizes. It also carries two negative controls:
 * the landscape branch must stay the legacy 6-column grid, and a wider portrait
 * canvas must move the icon proportionally - a constant-pixel implementation
 * would pass the 540 row and fail those.
 */
#include "ios_metrics.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char * what, int got, int want)
{
    printf("%s %-46s got %4d want %4d\n", got == want ? "ok  " : "FAIL", what, got, want);
    if(got != want) failures++;
}

static int icon_for(int width, int height)
{
    ios_metrics_t m;
    ios_metrics(width, height, &m);
    return m.icon;
}

int main(void)
{
    ios_metrics_t p;
    ios_metrics(540, 1170, &p);

    check("portrait flag", p.portrait, 1);
    check("portrait columns", p.columns, 4);
    check("portrait icon side", p.icon, 83);
    check("portrait column gap", p.gap_x, 44);
    check("portrait left margin", p.margin_x, 38);
    check("portrait label font", p.label_font, 15);
    check("portrait label gap", p.label_gap, 6);
    check("portrait label width", p.label_width, 125);
    check("portrait first row top", p.first_row_top, 110);
    check("portrait row pitch", p.row_pitch, 148);
    check("portrait rows before dock", p.rows_capacity, 6);
    check("portrait dock height", p.dock_height, 114);
    check("portrait dock top", p.dock_top, 1028);
    check("portrait dock inset", p.dock_margin_x, 11);
    check("portrait dock radius", p.dock_radius, 44);
    check("portrait home bar width", p.home_bar_w, 186);
    check("portrait home bar height", p.home_bar_h, 7);
    check("portrait home bar y", p.home_bar_y, 1152);
    check("portrait status height", p.status_height, 46);
    check("portrait status font", p.status_font, 22);
    check("portrait status pad top", p.status_pad_top, 14);
    check("portrait status pad left", p.status_pad_left, 28);
    check("portrait status wifi", p.status_wifi, 32);
    check("portrait status battery", p.status_battery, 44);

    int right_edge = p.margin_x + 3 * (p.icon + p.gap_x) + p.icon;
    check("last column right edge", right_edge, 540 - p.margin_x);
    check("column 0 centre", ios_column_center(&p, 0), p.margin_x + p.icon / 2);
    check("column pitch", ios_column_center(&p, 1) - ios_column_center(&p, 0),
          p.icon + p.gap_x);
    check("four rows fit above the dock",
          p.first_row_top + 3 * p.row_pitch + p.icon + p.label_gap + p.label_font
              < p.dock_top, 1);
    check("home bar sits inside the dock", p.home_bar_y > p.dock_top, 1);
    check("status strip ends above the first row", p.status_height < p.first_row_top, 1);

    /* Negative control 1: the landscape canvas is the untouched legacy grid. */
    ios_metrics_t l;
    ios_metrics(800, 480, &l);
    check("landscape flag", l.portrait, 0);
    check("landscape columns", l.columns, 6);
    check("landscape icon side", l.icon, 64);
    check("landscape column gap", l.gap_x, 69);
    check("landscape left margin", l.margin_x, 35);
    check("landscape rows spread flag", l.row_pitch, 0);
    check("landscape dock top", l.dock_top, 386);
    check("landscape home bar y", l.home_bar_y, 470);

    /* Negative control 2: a wider portrait canvas must scale, not stay fixed. */
    int wide = icon_for(780, 1690);
    check("wider canvas gets a wider icon", wide > p.icon, 1);
    double a = (double)p.icon / 540.0;
    double b = (double)wide / 780.0;
    check("icon fraction holds within 1/1000", fabs(a - b) < 0.001, 1);
    check("portrait is not the landscape constant", p.icon == l.icon, 0);

    printf("\n%d failures\n", failures);
    return failures != 0;
}
