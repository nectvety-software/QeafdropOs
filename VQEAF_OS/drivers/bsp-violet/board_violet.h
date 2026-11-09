/* SPDX-License-Identifier: MIT */
/* bsp/violet/board_violet.h - Violet board support (Xiaomi Redmi Note 7 Pro) */

#ifndef UMOS_BOARD_VIOLET_H
#define UMOS_BOARD_VIOLET_H

#include "hal/include/hal_board.h"

int violet_board_init(void);
void violet_display_driver(void);
void violet_touch_driver(void);
void violet_audio_driver(void);
void violet_storage_driver(void);
void violet_power_driver(void);
void violet_rtc_driver(void);
void violet_radio_driver(void);

#endif
