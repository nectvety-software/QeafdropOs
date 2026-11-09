/* SPDX-License-Identifier: MIT */
/* apps/usbui/usbui.h - "Use USB for" notification sheet.
 *
 * Shown when a USB cable to a PC is detected. Three choices: charge only,
 * transfer files (MTP), transfer photos (PTP). The sheet only QUEUES an
 * intent; the board rebinds the gadget through usb_gadget_set_mode().
 * Nothing is selected by default, so a stray tap cannot silently rebind USB.
 */

#ifndef UMOS_USBUI_H
#define UMOS_USBUI_H

#include <stdint.h>
#include "hal/include/usb_gadget.h"

#define USBUI_EV_TOUCH 4
#define USBUI_EV_BACK  7

enum usbui_action {
    USBUI_ACT_NONE = 0,
    USBUI_ACT_SET_MODE,
    USBUI_ACT_DISMISS
};

void usbui_init(void);
void usbui_open(void);
void usbui_close(void);
int  usbui_visible(void);
void usbui_draw(void);
int  usbui_event(uint32_t type, uint32_t d1, uint32_t d2);

/* Returns queued action; *arg is the enum usb_mode for SET_MODE. */
enum usbui_action usbui_take_action(int *arg);
int  usbui_row_count(void);

#endif
