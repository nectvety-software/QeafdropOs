/* SPDX-License-Identifier: MIT */
/* bsp/violet/usb_gadget.c - USB gadget driver for violet (DWC3) + FAT16 LUN */

#include "hal/include/usb_gadget.h"
#include "hal/include/display.h"
#include "hal/include/hal.h"
#include "fs/fat16.h"
#include "fs/storage.h"
#include "fs/rescue.h"
#include "core/log.h"
#include "core/sysio.h"

static struct fat16_volume g_lun_vol;
static uint8_t g_lun_data[FAT16_VOLUME_SIZE];
#if defined(__aarch64__)
/* On the device the volume lives in the tmpfs image configfs serves, so a byte
 * the host writes into QEAF.CMD is a byte this process reads back. A private
 * RAM array would make the two copies diverge on the first host write: the
 * phone would answer commands from the volume it built at boot. */
static uint8_t *g_lun_map;
#endif
static int g_gadget_started = 0;
static enum usb_mode g_mode = USB_MODE_CHARGE;
static int g_cable;

static int lun_read_sector(uint32_t lba, uint8_t *buf) {
    return fat16_read_sector(&g_lun_vol, lba, buf);
}

static int lun_write_sector(uint32_t lba, const uint8_t *buf) {
    return fat16_write_sector(&g_lun_vol, lba, buf);
}

static uint32_t lun_total_sectors(void) {
    return fat16_total_sectors(&g_lun_vol);
}

static struct usb_gadget_lun g_lun = {
    .read_sector = lun_read_sector,
    .write_sector = lun_write_sector,
    .total_sectors = lun_total_sectors,
    .vendor = "QEAF",
    .product = "RESCUE LUN",
    .revision = "1.0",
};

#include <stdint.h>
#if defined(__aarch64__)
/* The device link carries no libc headers (-nostdinc); the symbols
 * themselves come from libc_stubs.c. Declaring them here is the same
 * choice core/sysio.c makes for its syscalls, and it keeps this file in
 * the device build instead of host-gates-only. */
size_t strlen(const char *s);
#else
#include <string.h>
#endif


/* ---- sync protocol: DEVINFO.TXT -------------------------------------------
 * VQEAF Tool reads this instead of scraping QEAFBOOT.LOG. Only values the OS
 * actually knows are published: no invented serial, battery or build hash. */
static void put_u(char *buf, size_t cap, unsigned v) {
    char tmp[12];
    int t = 0;
    size_t n = strlen(buf);
    do { tmp[t++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (t > 0 && n + 1 < cap) buf[n++] = tmp[--t];
    buf[n] = '\0';
}

static void cat(char *buf, size_t cap, const char *s) {
    size_t n = strlen(buf);
    while (*s && n + 1 < cap) buf[n++] = *s++;
    buf[n] = '\0';
}

static void publish_devinfo(void) {
    char text[512];

    text[0] = '\0';
    cat(text, sizeof(text), "VQEAF-SYNC/1\n");
    cat(text, sizeof(text), "os=VQEAF OS 0.1.0\n");
    cat(text, sizeof(text), "device=violet\n");
    cat(text, sizeof(text), "control_lun=QEAFRESCUE\n");
    cat(text, sizeof(text), "usb_mode=");
    cat(text, sizeof(text), usb_gadget_mode_name(g_mode));
    cat(text, sizeof(text), "\nusb_cable=");
    put_u(text, sizeof(text), (unsigned)g_cable);
    cat(text, sizeof(text), "\norientation=");
    cat(text, sizeof(text), display_orientation_name(display_orientation()));
    cat(text, sizeof(text), "\nauto_rotate=");
    put_u(text, sizeof(text), (unsigned)display_auto_rotate());
    cat(text, sizeof(text), "\nstorage_folders=");
    put_u(text, sizeof(text), 5u);
    cat(text, sizeof(text), "\nsectors=");
    put_u(text, sizeof(text), fat16_total_sectors(&g_lun_vol));
    cat(text, sizeof(text), "\n");
    if (fat16_write_file(&g_lun_vol, "DEVINFO.TXT",
                         (const uint8_t *)text, strlen(text)) < 0)
        umos_logf("usb: DEVINFO.TXT refused by the volume builder\n");
    else
        umos_logf("usb: DEVINFO.TXT published for the PC tool\n");
}

const char *usb_gadget_mode_name(enum usb_mode mode) {
    switch (mode) {
    case USB_MODE_CHARGE: return "charge";
    case USB_MODE_MTP:    return "mtp";
    case USB_MODE_PTP:    return "ptp";
    default:              return "unknown";
    }
}

enum usb_mode usb_gadget_mode(void) { return g_mode; }

int usb_gadget_set_mode(enum usb_mode mode) {
    if (mode < 0 || mode >= USB_MODE_COUNT) return -1;
    if (!g_gadget_started) return -1;      /* no gadget to reconfigure yet */
    g_mode = mode;
    /* A real DWC3 would rebind the function here. Every mode keeps the
     * control LUN: losing it would lock the PC tool out of the device. */
    umos_logf("usb: mode -> %s (control LUN stays bound)\n",
              usb_gadget_mode_name(mode));
    publish_devinfo();
    return 0;
}

int usb_gadget_cable(void) { return g_cable; }

void usb_gadget_set_cable(int plugged) {
    int was = g_cable;
    g_cable = plugged ? 1 : 0;
    if (was != g_cable)
        umos_logf("usb: cable %s\n", g_cable ? "plugged" : "unplugged");
    if (!g_cable && g_mode != USB_MODE_CHARGE)
        g_mode = USB_MODE_CHARGE;   /* unplug drops data mode, back to safe */
    publish_devinfo();
}

struct fat16_volume *usb_gadget_volume(void) { return &g_lun_vol; }

void usb_gadget_poll(void) {
    static enum display_orientation last_o = DISPLAY_PORTRAIT;
    /* Run the command loop first: a `rotate` verb can change the orientation,
     * and DEVINFO.TXT must be republished in the SAME tick, not one late. */
    rescue_poll(&g_lun_vol);
    if (display_orientation() != last_o) {
        last_o = display_orientation();
        publish_devinfo();
    }
}

int usb_gadget_init(void) {
    g_mode = USB_MODE_CHARGE;
    g_cable = 0;
    umos_logf("usb: gadget init\n");
    {
        uint8_t *vol = g_lun_data;
#if defined(__aarch64__)
        if (sysio_map_file(USB_GADGET_LUN_IMAGE, FAT16_VOLUME_SIZE,
                           (void **)&g_lun_map) == 0 && g_lun_map) {
            vol = g_lun_map;
            umos_logf("usb: volume mapped at %s (%u bytes)\n",
                      USB_GADGET_LUN_IMAGE, (unsigned)FAT16_VOLUME_SIZE);
        } else {
            umos_logf("usb: no LUN image at %s, volume stays private to RAM\n",
                      USB_GADGET_LUN_IMAGE);
        }
#endif
        /* Initialize the FAT16 volume the LUN serves. */
        if (fat16_init(&g_lun_vol, vol, FAT16_VOLUME_SIZE) < 0) {
            umos_logf("usb: FAT16 init failed\n");
            return -1;
        }
        /* The drive a host opens has to be the storage the phone talks about,
         * so the five folders are part of the volume's shape, not an
         * afterthought. */
        if (storage_publish(&g_lun_vol) != UMOS_STORAGE_COUNT) {
            umos_logf("usb: storage folders missing, LUN stays bare\n");
            return -1;
        }
        publish_devinfo();
        rescue_init(&g_lun_vol);
    }
    return 0;
}

int usb_gadget_start(void) {
    if (g_gadget_started) return 0;
    if (usb_gadget_add_lun(&g_lun) < 0) {
        umos_logf("usb: add LUN failed\n");
        return -1;
    }
    g_gadget_started = 1;
    umos_logf("usb: gadget started (control LUN %u sectors, mode %s)\n",
              fat16_total_sectors(&g_lun_vol), usb_gadget_mode_name(g_mode));
    return 0;
}

int usb_gadget_stop(void) {
    g_gadget_started = 0;
    umos_logf("usb: gadget stopped\n");
    return 0;
}

int usb_gadget_add_lun(struct usb_gadget_lun *lun) {
    (void)lun;
    umos_logf("usb: LUN added: %s %s\n", g_lun.vendor, g_lun.product);
    return 0;
}

int usb_gadget_set_rndis_mac(const uint8_t mac[6]) {
    (void)mac;
    umos_logf("usb: RNDIS MAC set\n");
    return 0;
}

int usb_gadget_set_ncm_mac(const uint8_t mac[6]) {
    (void)mac;
    umos_logf("usb: NCM MAC set\n");
    return 0;
}
