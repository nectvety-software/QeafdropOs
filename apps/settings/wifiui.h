/* SPDX-License-Identifier: MIT */
/* apps/settings/wifiui.h - Wi-Fi settings screens: network list + details.
 *
 * Like the recovery screen, this GUI performs no association itself. A tap on
 * a network queues WIFIUI_ACT_CONNECT and the executor (init / board) calls
 * wifi_connect(); the screen then re-reads the seam and shows what the radio
 * reports. The master switch is the one exception: radio power is reversible
 * and unprivileged, so it flips wifi_on()/wifi_off() in place. Every figure on
 * the details page comes from wifi_link_info(), and a board that cannot answer
 * gets a line saying so instead of a datasheet number.
 */

#ifndef UMOS_WIFIUI_H
#define UMOS_WIFIUI_H

#include <stdint.h>

/* Intents handed to the executor. The GUI only requests them. */
enum wifiui_action {
    WIFIUI_ACT_NONE = 0,
    WIFIUI_ACT_CONNECT,        /* arg = index into the last scan */
    WIFIUI_ACT_DISCONNECT,
    WIFIUI_ACT_MANAGE_SAVED,
    WIFIUI_ACT_SCAN_SETTINGS,
    WIFIUI_ACT_NET_PREFS,
    WIFIUI_ACT_COUNT
};

/* Input events. Touch carries panel pixels, like recovery's. */
enum wifiui_event {
    WIFIUI_EV_TOUCH = 1,
    WIFIUI_EV_BACK  = 2,
};

void wifiui_init(void);
void wifiui_open(void);
void wifiui_close(void);
int  wifiui_visible(void);
void wifiui_draw(void);
int  wifiui_event(uint32_t type, uint32_t d1, uint32_t d2);

/* Consumes one pending intent; arg may be NULL. */
enum wifiui_action wifiui_take_action(int *arg);

/* State the host gate measures. */
int wifiui_page(void);        /* 0 = list, 1 = details */
int wifiui_master_on(void);
int wifiui_adv_open(void);
int wifiui_pref(int i);       /* 0 auto-on, 1 open notify, 2 wi-fi direct */
int wifiui_rows(void);        /* scan rows on screen */
int wifiui_scan_failed(void); /* 1: radio on, but the seam refused to answer */

#endif
