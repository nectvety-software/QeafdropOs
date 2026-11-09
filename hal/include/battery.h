/* SPDX-License-Identifier: MIT */
/* hal/include/battery.h - power_supply seam: what the battery really says.

 * Values are read from /sys/class/power_supply nodes on every read; nothing
 * is cached between them, because a charge that stopped is exactly the fact
 * a screen has to show. A board whose nodes are missing answers < 0, and the
 * caller draws nothing rather than drawing a confident lie.
 */

#ifndef UMOS_BATTERY_H
#define UMOS_BATTERY_H

#include <stddef.h>

struct battery_state {
    int capacity;   /* 0..100, or -1 when the node did not answer */
    int charging;   /* battery/status is Charging (or Charging/Discharging) */
    int plugged;    /* usb/online or dc/online says 1 */
    char status[16];/* the status word verbatim, minus newline */
};

/* 0 when the board answered (status non-empty and capacity >= 0), < 0 for
 * "no answer" - out is untouched in that case. */
int battery_read(struct battery_state *out);

/* Point the seam somewhere other than /sys: the host gate hands it a
 * fixture tree of ordinary files. Device code never calls this. */
void battery_set_sysfs_root(const char *root);

#endif
