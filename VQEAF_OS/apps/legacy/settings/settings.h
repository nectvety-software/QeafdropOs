/* SPDX-License-Identifier: MIT */
/* apps/settings/settings.h - Settings hub hosting the Wi-Fi and Bluetooth
 * screens. One window, two sub-pages; see settings.c. */

#ifndef UMOS_SETTINGS_H
#define UMOS_SETTINGS_H

#include <stdint.h>

void settings_init(void);
void settings_open(void);
void settings_draw(void);
int  settings_event(uint32_t type, uint32_t d1, uint32_t d2);

/* 0 = hub, 1 = Wi-Fi, 2 = Bluetooth, 3 = keyboard, 4 = features, 5 = language.
 * Measured by the host gate. */
int settings_page(void);

#endif
