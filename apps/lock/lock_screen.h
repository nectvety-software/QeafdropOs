/* SPDX-License-Identifier: MIT */
/* apps/lock/lock_screen.h */

#ifndef UMOS_LOCK_SCREEN_H
#define UMOS_LOCK_SCREEN_H

void lock_init(void);

/* 1 when the battery seam answered AND the charger is plugged: the state the
 * host gate measures. The charge line itself is drawn from lock_draw(). */
int lock_charge_visible(void);

#endif
