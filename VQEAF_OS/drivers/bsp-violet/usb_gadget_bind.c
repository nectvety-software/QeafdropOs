/* SPDX-License-Identifier: MIT */
/* bsp/violet/usb_gadget_bind.c - put the rescue volume on the wire.
 *
 * Why this file exists: usb_gadget_init() builds a FAT16 volume in RAM and
 * usb_gadget_start() only logged that a LUN had been "added". Nothing ever
 * touched configfs or wrote a UDC, so an image built from this tree booted and
 * enumerated NOTHING on USB - no mass storage, no adb, no device node at all.
 * That is the measured shape of the m48 / m49 / m49b flashes: fastboot wrote
 * the partition, `reboot recovery` was accepted, and afterwards the host saw no
 * USB device for minutes at 1 s resolution.
 *
 * The sequence below is the one the old tree actually ran on this board
 * (out/tmp/oldsrc/init/lun.c: `/run/qeaf-lun.img` plus
 * `lun: binding UDC a600000.dwc3`): configfs gadget g1, f_mass_storage lun.0
 * pointed at the tmpfs image the volume lives in, then the UDC name read out of
 * /sys/class/udc instead of assumed.
 *
 * Three rules are kept from that tree because they were measured, not chosen:
 *
 *   - the UDC name is scanned. Not every violet kernel exposes
 *     a600000.dwc3 under that name, and a bind to a controller that does not
 *     exist fails silently - which is the failure class this file removes.
 *   - the volume is a tmpfs file, not a private array, so the bytes the host
 *     writes into QEAF.CMD are the bytes this process reads back. See
 *     usb_gadget_init().
 *   - everything that can block runs in a child. Violet has no watchdog
 *     driver: PID 1 parked in the gadget stack is a board that only a power
 *     hold recovers.
 */

#include "hal/include/usb_gadget.h"
#include "core/sysio.h"
#include "core/log.h"

#define GADGET_MOUNT  "/config"
#define GADGET_DIR    "/config/usb_gadget/g1"
#define GADGET_STR    "/config/usb_gadget/g1/strings/0x409"
#define GADGET_CFG    "/config/usb_gadget/g1/configs/c.1"
#define GADGET_CFGSTR "/config/usb_gadget/g1/configs/c.1/strings/0x409"
#define GADGET_FUN    "/config/usb_gadget/g1/functions/mass_storage.0"
#define GADGET_LUN    "/config/usb_gadget/g1/functions/mass_storage.0/lun.0"
#define GADGET_UDC    "/config/usb_gadget/g1/UDC"
#define UDC_DIR       "/sys/class/udc"

/* 45 s at 100 ms. The earlier bound was 8 s and every recovery boot before
 * 2026-10-26 failed there without anything readable saying so: a dwc3 that
 * defers its probe on the PHY or the PMIC needs longer than a fast SoC boot. */
#define UDC_WAIT_TICKS 450

static void wr(const char *path, const char *val) {
    int rc = sysio_write_str(path, val);
    if (rc != 0)
        umos_logf("gadget: %s <- '%s' refused (%d)\n", path, val, rc);
}

static int make_dirs(void) {
    static const char *const dirs[] = {
        GADGET_DIR, GADGET_STR, GADGET_CFG, GADGET_CFGSTR,
        GADGET_FUN, GADGET_LUN,
    };
    unsigned i;

    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        int rc = sysio_mkdir_p(dirs[i]);
        if (rc != 0) {
            umos_logf("gadget: cannot create %s (%d)\n", dirs[i], rc);
            return -1;
        }
    }
    return 0;
}

int qeaf_gadget_bind(void) {
    char udc[64];
    int ticks = 0;

    if (sysio_mount("configfs", GADGET_MOUNT, "configfs") != 0) {
        umos_logf("gadget: no configfs at %s - the LUN cannot be built\n",
                  GADGET_MOUNT);
        return -1;
    }
    if (make_dirs() != 0)
        return -1;

    /* The ids the ROM's own mass-storage configuration uses, deliberately.
     * The host tools look for QEAFRESCUE by vendor 18d1 / product 0015 and by
     * the serial below; enumerating under other ids would be a working gadget
     * that nothing on the PC side finds. */
    wr(GADGET_DIR "/idVendor", "0x18D1");
    wr(GADGET_DIR "/idProduct", "0x0015");
    wr(GADGET_DIR "/bcdUSB", "0x0200");
    wr(GADGET_DIR "/bcdDevice", "0x0100");
    wr(GADGET_STR "/manufacturer", "VQEAF OS");
    wr(GADGET_STR "/product", "Rescue Volume");
    wr(GADGET_STR "/serialnumber", "QEAFRESCUE001");

    wr(GADGET_CFG "/MaxPower", "200");
    wr(GADGET_CFGSTR "/configuration", "QeafRescue");

    wr(GADGET_LUN "/cdrom", "0");
    /* ro=0 is the point of the channel: the host has to be able to drop a
     * command into the volume and the phone has to answer into the same bytes. */
    wr(GADGET_LUN "/ro", "0");
    wr(GADGET_LUN "/removable", "1");
    wr(GADGET_LUN "/file", USB_GADGET_LUN_IMAGE);

    if (sysio_symlink(GADGET_FUN, GADGET_CFG "/f1") != 0) {
        umos_logf("gadget: cannot attach mass_storage to the configuration\n");
        return -1;
    }

    /* Polled because the volume is built before the device scan that used to
     * give the controller time to register: /sys/class/udc is filled by the
     * class core when dwc3 probes, and on a board that boots straight into
     * init that probe can still be in flight when the gadget is ready. */
    for (;;) {
        udc[0] = '\0';
        if (sysio_first_entry(UDC_DIR, "", udc, (int)sizeof(udc)) == 0 && udc[0])
            break;
        if (ticks >= UDC_WAIT_TICKS) {
            umos_logf("gadget: %s stayed empty for %d s, no controller to bind\n",
                      UDC_DIR, UDC_WAIT_TICKS / 10);
            return -1;
        }
        sysio_sleep_ms(100);
        ticks++;
    }

    umos_logf("gadget: binding UDC %s\n", udc);
    if (sysio_write_str(GADGET_UDC, udc) != 0) {
        umos_logf("gadget: UDC bind refused - volume built, nothing on the wire\n");
        return -1;
    }
    umos_logf("gadget: LUN 0 bound to %s at %s\n", udc, USB_GADGET_LUN_IMAGE);
    return 0;
}

int qeaf_gadget_start_detached(void) {
    int pid = sysio_fork();

    if (pid == 0) {
        int rc = qeaf_gadget_bind();
        sysio_exit(rc == 0 ? 0 : 1);
        return 0;              /* not reached: the child is gone */
    }
    if (pid < 0) {
        /* No fork available. A bind that might block is still better than a
         * gadget that is never bound; this is also the host branch, where
         * nothing binds either way and the log says why. */
        umos_logf("gadget: no child (%d), binding inline\n", pid);
        return qeaf_gadget_bind();
    }
    umos_logf("gadget: bind runs in pid %d\n", pid);
    return 0;
}
