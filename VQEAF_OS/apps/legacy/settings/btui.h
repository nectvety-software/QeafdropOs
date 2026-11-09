/* SPDX-License-Identifier: MIT */
/* apps/settings/btui.h - Bluetooth settings: device list + device details.
 *
 * Same contract as wifiui.h: the GUI never bonds or connects anything by
 * itself. A tap queues BTUI_ACT_CONNECT / BTUI_ACT_PAIR and the executor
 * calls bt_connect(); every figure on the details page comes from
 * bt_link_info(), and a board that cannot answer gets "no answer" instead of
 * an invented number. The master switch is the one reversible exception: it
 * flips bt_on()/bt_off() in place.
 */

#ifndef UMOS_BTUI_H
#define UMOS_BTUI_H

#include <stdint.h>

enum btui_action {
    BTUI_ACT_NONE = 0,
    BTUI_ACT_CONNECT,          /* arg = index into the last scan */
    BTUI_ACT_DISCONNECT,
    BTUI_ACT_PAIR,             /* arg = index */
    BTUI_ACT_UNPAIR,
    BTUI_ACT_SCAN_SETTINGS,
    BTUI_ACT_NET_PREFS,
    BTUI_ACT_SAVED,
    BTUI_ACT_SCAN,
    BTUI_ACT_PAIR_NEW,
    BTUI_ACT_COUNT
};

enum btui_event {
    BTUI_EV_TOUCH = 1,
    BTUI_EV_BACK  = 2
};

void btui_init(void);
void btui_open(void);
void btui_close(void);
int  btui_visible(void);
void btui_draw(void);
int  btui_event(uint32_t type, uint32_t d1, uint32_t d2);

enum btui_action btui_take_action(int *arg);

/* State the host gate measures. */
int btui_page(void);           /* 0 = list, 1 = details */
int btui_master_on(void);
int btui_adv_open(void);
int btui_profile(int i);       /* 0..4 */
int btui_rows(void);           /* total scan rows */
int btui_paired_rows(void);
int btui_scan_failed(void);
int btui_details_index(void);  /* device shown on the details page */

#endif
