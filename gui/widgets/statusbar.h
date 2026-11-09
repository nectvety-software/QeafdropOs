/* SPDX-License-Identifier: MIT */
/* gui/widgets/statusbar.h - Status bar (top bar) */

#ifndef UMOS_STATUSBAR_H
#define UMOS_STATUSBAR_H

#include "gfx.h"
#include "font.h"

struct statusbar {
    struct font *font;
    void (*on_wifi_click)(void);
    void (*on_battery_click)(void);
    void (*on_clock_click)(void);
};

void statusbar_init(struct statusbar *sb, struct font *f);
void statusbar_draw(struct statusbar *sb);
void statusbar_set_time(struct statusbar *sb, const char *time);
void statusbar_set_battery(struct statusbar *sb, int level); /* 0-100 */
void statusbar_set_wifi(struct statusbar *sb, int rssi); /* dBm, e.g. -60 */
void statusbar_set_signal(struct statusbar *sb, int bars); /* 0-4 */

#endif
