/* SPDX-License-Identifier: MIT */
/* hal/include/usb_gadget.h - USB gadget (RNDIS/NCM mass storage LUN) */

#ifndef UMOS_USB_GADGET_H
#define UMOS_USB_GADGET_H

#include <stdint.h>
#include <stddef.h>

#define USB_GADGET_MAX_LUNS 1

/* The tmpfs image the configfs LUN points at. One path, two users: the volume
 * the phone edits in usb_gadget_poll() and the file f_mass_storage serves have
 * to be the same bytes, or a host command sits in the file while the phone
 * keeps answering from a copy it froze at boot. */
#define USB_GADGET_LUN_IMAGE "/run/qeaf-lun.img"

struct usb_gadget_lun {
    int (*read_sector)(uint32_t lba, uint8_t *buf);
    int (*write_sector)(uint32_t lba, const uint8_t *buf);
    uint32_t (*total_sectors)(void);
    const char *vendor;
    const char *product;
    const char *revision;
};

/* What the user picked in the "Use USB for" notification. Charging is the
 * default and the only mode with no data function bound. The rescue LUN is a
 * CONTROL channel, not one of these choices: it stays present in all three
 * modes so a PC tool can always manage the device (and so the user can always
 * change their mind). */
enum usb_mode {
    USB_MODE_CHARGE = 0,        /* VBUS only, no data endpoint */
    USB_MODE_MTP,               /* files  + control LUN */
    USB_MODE_PTP,               /* photos + control LUN */
    USB_MODE_COUNT
};

/* Returns the active mode. CHARGE until the user picks otherwise. */
enum usb_mode usb_gadget_mode(void);
int  usb_gadget_set_mode(enum usb_mode mode);
const char *usb_gadget_mode_name(enum usb_mode mode);

/* VBUS present? On the board this reads the charger/USB PHY; the host gate
 * drives it through usb_gadget_set_cable() so the notification can be tested
 * against a real framebuffer without a phone. */
int  usb_gadget_cable(void);
void usb_gadget_set_cable(int plugged);

/* The RAM-backed volume the LUN serves. The rescue command loop reads and
 * writes QEAF.CMD / RESPONSE.TXT through this same object, so host writes
 * and phone answers are literally the same bytes. */
struct fat16_volume;
struct fat16_volume *usb_gadget_volume(void);
/* One tick of the QEAF.CMD reader; called from the init main loop. */
void usb_gadget_poll(void);

int usb_gadget_init(void);
int usb_gadget_start(void);
int usb_gadget_stop(void);
int usb_gadget_add_lun(struct usb_gadget_lun *lun);
int usb_gadget_set_rndis_mac(const uint8_t mac[6]);
int usb_gadget_set_ncm_mac(const uint8_t mac[6]);

/* The real bind (bsp/violet/usb_gadget_bind.c): configfs gadget, one
 * f_mass_storage LUN over USB_GADGET_LUN_IMAGE, then the UDC name found in
 * /sys/class/udc. usb_gadget_start() above only registers the LUN with this
 * process - without this bind the phone enumerates nothing on USB. */
int qeaf_gadget_bind(void);
/* Same, forked: a dwc3 probe or a slow tmpfs cannot park PID 1. */
int qeaf_gadget_start_detached(void);

#endif
