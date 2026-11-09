/* SPDX-License-Identifier: MIT */
/* platform/violet/usb_lun.c - the QEAFRESCUE drive as a real USB LUN.
 *
 * QeafdropOs builds the FAT16 volume in a RAM array and its usb_gadget_add_lun
 * is only a stub. Here the same fs/fat16.c volume lives in a 32 MiB file
 * mapped from tmpfs, and the kernel's f_mass_storage function serves it over
 * configfs. Because both sides use the same page cache, a file the PC drops
 * on the drive is immediately visible to fs/rescue.c's QEAF.CMD reader.
 */
#define _GNU_SOURCE

#include "usb_lun.h"

#include "violet.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fs/rescue.h"
#include "fs/storage.h"
#include "qeaf_version.h"

#define LUN_PATH "/run/qeaf-lun.img"
#define GADGET_DIR "/config/usb_gadget/homo"

static struct vio_lun *g_lun;

static int write_attr(const char *dir, const char *name, const char *value)
{
    char path[320];
    int length = snprintf(path, sizeof(path), "%s/%s", dir, name);
    if(length <= 0 || (size_t)length >= sizeof(path)) return -1;
    if(vio_write_file(path, value) < 0) {
        vio_log("usb: write %s failed: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int make_dir(const char *path)
{
    if(mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
    vio_log("usb: mkdir %s failed: %s", path, strerror(errno));
    return -1;
}

static void link_function(const char *config_dir, const char *function)
{
    char link_path[320];
    const char *name = strrchr(function, '/');
    if(name == NULL) return;
    name++;
    if(snprintf(link_path, sizeof(link_path), "%s/%s", config_dir, name) >=
       (int)sizeof(link_path)) return;
    /* The target must be the absolute configfs path of the function item.
     * A relative "../../functions/..." target does not register the function
     * with the gadget config on this kernel: the symlink is created but the
     * config's func_list stays empty, so UDC bind fails with
     * "Config c/1 of homo needs at least one function" (-EINVAL) and the host
     * never sees the LUN. Absolute targets are what init.usb.rc uses. */
    if(symlink(function, link_path) < 0 && errno != EEXIST)
        vio_log("usb: symlink %s failed: %s", link_path, strerror(errno));
}

static int find_udc(char *buf, size_t cap)
{
    DIR *dir = opendir("/sys/class/udc");
    struct dirent *entry;
    if(dir == NULL) return -1;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') continue;
        snprintf(buf, cap, "%s", entry->d_name);
        closedir(dir);
        return 0;
    }
    closedir(dir);
    return -1;
}

static void publish_devinfo(void)
{
    char text[512];
    int length;

    if(g_lun == NULL || !g_lun->ready) return;
    length = snprintf(text, sizeof(text),
                      "VQEAF-SYNC/1\n"
                      "os=" QEAF_OS_NAME " (violet)\n"
                      "device=violet\n"
                      "control_lun=QEAFRESCUE\n"
                      "usb_mode=%s\n"
                      "usb_cable=%d\n"
                      "storage_folders=%d\n"
                      "sectors=%u\n"
                      "ui=lvgl-9.4\n"
                      "panel=%dx%d\n",
                      usb_gadget_mode_name(g_lun->mode),
                      g_lun->cable, UMOS_STORAGE_COUNT,
                      (unsigned)fat16_total_sectors(&g_lun->volume),
                      VIO_PANEL_W, VIO_PANEL_H);
    if(length <= 0 || (size_t)length >= sizeof(text)) return;
    if(fat16_write_file(&g_lun->volume, "DEVINFO.TXT",
                        (const uint8_t *)text, (size_t)length) < 0)
        vio_log("usb: DEVINFO.TXT could not be published");
}

/* fs/fat16.c only ever grows a file inside the clusters reserved by its first
 * write: rewriting QEAFBOOT.LOG or RESPONSE.TXT in place fails once the text
 * outgrows that first allocation. Reserve the full 8 KiB channel caps now, so
 * fs/rescue.c's 8 KiB buffers can actually be published and rewritten. */
static void reserve_channel_files(struct fat16_volume *vol)
{
    static const uint8_t reserve[8192];
    (void)fat16_write_file(vol, "QEAFBOOT.LOG", reserve, sizeof(reserve));
    (void)fat16_write_file(vol, "RESPONSE.TXT", reserve, sizeof(reserve));
}

/* Reserve the whole SCREEN.RAW chain before the USB gadget binds. A host that
 * mounts the LUN writes its own files (System Volume Information, QEAF.CMD);
 * if `snap` allocated 61 clusters only when the verb ran, those clusters would
 * overlap files Windows already owns and both the dump and QEAF.CMD would be
 * corrupted. The first write is what fixes the chain; later `snap` calls only
 * rewrite the same clusters in place. */
static void reserve_screen_file(struct fat16_volume *vol)
{
    static uint8_t reserve[RESCUE_SCREEN_BYTES];
    (void)fat16_write_file(vol, "SCREEN.RAW", reserve, sizeof(reserve));
}

/* HOMOLOG.TXT carries the boot trace (vio_log tail + kernel ring tail) so the
 * host can read which DRM/touch step failed even though /dev/kmsg writes do
 * not land in the kernel ring. 16 KiB vio tail + 4 KiB kernel tail + marker. */
#define HOMOLOG_CAP 22528

static void reserve_homo_log(struct fat16_volume *vol)
{
    static const uint8_t reserve[HOMOLOG_CAP];
    (void)fat16_write_file(vol, "HOMOLOG.TXT", reserve, sizeof(reserve));
}

static void publish_homo_log(void)
{
    static char text[HOMOLOG_CAP];
    const char *tail = vio_log_tail();
    size_t used = 0;
    int klen;

    if(g_lun == NULL || !g_lun->ready) return;
    if(tail != NULL && tail[0] != '\0') {
        size_t n = strlen(tail);
        if(n >= sizeof(text) - 64) n = sizeof(text) - 64;
        memcpy(text, tail, n);
        used = n;
    }
    if(used + 32 < sizeof(text)) {
        memcpy(text + used, "\n--- kernel ring tail ---\n", 26);
        used += 26;
    }
    klen = vio_kernel_tail(text + used, sizeof(text) - used - 1);
    if(klen > 0) used += (size_t)klen;
    if(used == 0) memcpy(text, "(boot log empty)\n", 18);
    if(fat16_write_file(&g_lun->volume, "HOMOLOG.TXT",
                        (const uint8_t *)text, used) < 0)
        vio_log("usb: HOMOLOG.TXT could not be published");
}

int vio_lun_init(struct vio_lun *lun)
{
    memset(lun, 0, sizeof(*lun));
    lun->fd = -1;
    lun->mode = USB_MODE_CHARGE;

    lun->fd = open(LUN_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if(lun->fd < 0) {
        vio_log("lun: cannot create %s: %s", LUN_PATH, strerror(errno));
        return -1;
    }
    if(ftruncate(lun->fd, (off_t)FAT16_VOLUME_SIZE) < 0) {
        vio_log("lun: ftruncate failed: %s", strerror(errno));
        close(lun->fd);
        lun->fd = -1;
        return -1;
    }
    lun->size = FAT16_VOLUME_SIZE;
    lun->map = mmap(NULL, lun->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    lun->fd, 0);
    if(lun->map == MAP_FAILED) {
        lun->map = NULL;
        vio_log("lun: mmap failed: %s", strerror(errno));
        close(lun->fd);
        lun->fd = -1;
        return -1;
    }

    if(fat16_init(&lun->volume, lun->map, lun->size) < 0) {
        vio_log("lun: fat16_init failed");
        return -1;
    }
    if(storage_publish(&lun->volume) < 0) {
        vio_log("lun: storage_publish failed");
        return -1;
    }
    reserve_channel_files(&lun->volume);
    reserve_homo_log(&lun->volume);
    reserve_screen_file(&lun->volume);
    rescue_init(&lun->volume);
    lun->ready = 1;
    g_lun = lun;
    publish_devinfo();
    vio_log("lun: QEAFRESCUE volume ready (%u sectors, %u bytes)",
            (unsigned)fat16_total_sectors(&lun->volume),
            (unsigned)lun->size);
    return 0;
}

int vio_lun_start_gadget(struct vio_lun *lun)
{
    char strings[320], config[320], function[320], lun_dir[320];
    const char *udc;

    if(!lun->ready) return -1;
    g_lun = lun;

    vio_mkdir_p(GADGET_DIR, 0755);
    if(make_dir(GADGET_DIR) < 0) return -1;

    /* A previous bind (for example after a soft restart) must be released
     * before the UDC can be programmed again. */
    write_attr(GADGET_DIR, "UDC", "\n");

    write_attr(GADGET_DIR, "idVendor", "0x1d6b");
    write_attr(GADGET_DIR, "idProduct", "0x0104");
    write_attr(GADGET_DIR, "bcdUSB", "0x0200");
    write_attr(GADGET_DIR, "bcdDevice", "0x0100");

    snprintf(strings, sizeof(strings), "%s/strings/0x409", GADGET_DIR);
    make_dir(strings);
    write_attr(strings, "serialnumber", "qeafdropos-violet");
    write_attr(strings, "manufacturer", QEAF_OS_NAME);
    write_attr(strings, "product", "QEAFRESCUE");

    snprintf(config, sizeof(config), "%s/configs/c.1", GADGET_DIR);
    make_dir(config);
    snprintf(strings, sizeof(strings), "%s/strings/0x409", config);
    make_dir(strings);
    write_attr(strings, "configuration", "mass_storage");
    write_attr(config, "MaxPower", "500");

    snprintf(function, sizeof(function), "%s/functions/mass_storage.0", GADGET_DIR);
    make_dir(function);
    snprintf(lun_dir, sizeof(lun_dir), "%s/lun.0", function);
    make_dir(lun_dir);
    write_attr(lun_dir, "cdrom", "0");
    write_attr(lun_dir, "ro", "0");
    write_attr(lun_dir, "nofua", "0");
    write_attr(lun_dir, "removable", "1");
    write_attr(lun_dir, "file", LUN_PATH);

    link_function(config, function);

    if(find_udc(lun->udc, sizeof(lun->udc)) < 0) {
        vio_log("usb: no UDC registered; LUN stays in RAM");
        return -1;
    }
    udc = lun->udc;
    if(write_attr(GADGET_DIR, "UDC", udc) < 0) {
        vio_log("usb: binding UDC %s failed", udc);
        return -1;
    }
    lun->gadget = 1;
    vio_log("usb: mass storage bound to %s (%s)", udc, LUN_PATH);
    return 0;
}

static int read_cable(struct vio_lun *lun)
{
    char path[256], state[64];
    int online = 0;

    if(lun->udc[0]) {
        snprintf(path, sizeof(path), "/sys/class/udc/%s/state", lun->udc);
        if(vio_read_file(path, state, sizeof(state)) > 0) {
            if(strstr(state, "configured") != NULL) return 1;
        }
    }
    if(vio_read_int("/sys/class/power_supply/usb/online", &online) == 0 && online)
        return 1;
    if(vio_read_int("/sys/class/power_supply/usb/present", &online) == 0 && online)
        return 1;
    return 0;
}

void vio_lun_poll(struct vio_lun *lun)
{
    int cable;
    if(lun == NULL || !lun->ready) return;
    g_lun = lun;
    cable = read_cable(lun);
    if(cable != lun->cable) {
        lun->cable = cable;
        vio_log("usb: cable %s", cable ? "connected" : "removed");
        publish_devinfo();
    }
    publish_homo_log();
}

/* ---- the hal/include/usb_gadget.h contract used by fs/rescue.c ---------- */

enum usb_mode usb_gadget_mode(void)
{
    return g_lun ? g_lun->mode : USB_MODE_CHARGE;
}

int usb_gadget_set_mode(enum usb_mode mode)
{
    if(g_lun == NULL || !g_lun->ready) return -1;
    if(mode < 0 || mode >= USB_MODE_COUNT) return -1;
    g_lun->mode = mode;
    publish_devinfo();
    return g_lun->gadget ? 0 : -1;
}

const char *usb_gadget_mode_name(enum usb_mode mode)
{
    switch(mode) {
    case USB_MODE_MTP: return "mtp";
    case USB_MODE_PTP: return "ptp";
    default:           return "charge";
    }
}

int usb_gadget_cable(void)
{
    return g_lun ? g_lun->cable : 0;
}

void usb_gadget_set_cable(int plugged)
{
    if(g_lun == NULL) return;
    g_lun->cable = plugged ? 1 : 0;
    publish_devinfo();
}

struct fat16_volume *usb_gadget_volume(void)
{
    return g_lun ? &g_lun->volume : NULL;
}

void usb_gadget_poll(void)
{
    if(g_lun) vio_lun_poll(g_lun);
}

int usb_gadget_init(void)
{
    return (g_lun && g_lun->ready) ? 0 : -1;
}

int usb_gadget_start(void)
{
    return (g_lun && g_lun->gadget) ? 0 : -1;
}

int usb_gadget_stop(void)
{
    if(g_lun == NULL) return -1;
    write_attr(GADGET_DIR, "UDC", "\n");
    g_lun->gadget = 0;
    return 0;
}

int usb_gadget_add_lun(struct usb_gadget_lun *lun)
{
    (void)lun;
    return -1;
}

int usb_gadget_set_rndis_mac(const uint8_t mac[6])
{
    (void)mac;
    return -1;
}

int usb_gadget_set_ncm_mac(const uint8_t mac[6])
{
    (void)mac;
    return -1;
}
