/* SPDX-License-Identifier: MIT */
/* platform/violet/usb_lun.h - QEAFRESCUE FAT16 LUN over USB configfs. */
#ifndef HOMO_USB_LUN_H
#define HOMO_USB_LUN_H

#include <stdint.h>

#include "fs/fat16.h"
#include "hal/include/usb_gadget.h"

struct vio_lun {
    int fd;
    uint8_t *map;
    size_t size;
    struct fat16_volume volume;
    int ready;
    int gadget;
    int cable;
    enum usb_mode mode;
    char udc[64];
};

int vio_lun_init(struct vio_lun *lun);
int vio_lun_start_gadget(struct vio_lun *lun);
void vio_lun_poll(struct vio_lun *lun);

#endif /* HOMO_USB_LUN_H */
