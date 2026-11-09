/* SPDX-License-Identifier: MIT */
/* platform/violet/devnodes.c - mounts, sysfs-driven /dev, logging.
 *
 * The violet kernel is built with # CONFIG_DEVTMPFS is not set and the
 * initramfs is the root filesystem. Nothing populates /dev for us, so every
 * node is created here by reading the major:minor pair the kernel publishes
 * under /sys/class. This file is deliberately dependency-free: it runs before
 * LVGL exists and must keep working when the panel does not.
 */
#define _GNU_SOURCE

#include "violet.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

static int kmsg_fd = -1;

/* Every vio_log line is also kept in RAM: the rescue volume is the only log
 * the host can read back (QEAFBOOT.LOG is owned by fs/rescue.c), so usb_lun.c
 * publishes this tail as HOMOLOG.TXT every poll. */
static char vio_tail[16384];
static size_t vio_tail_len;

static void vio_append(const char *text)
{
    size_t n = strlen(text);

    if(n >= sizeof(vio_tail)) n = sizeof(vio_tail) - 1;
    if(vio_tail_len + n + 2 > sizeof(vio_tail)) {
        size_t keep = sizeof(vio_tail) / 2;
        memmove(vio_tail, vio_tail + vio_tail_len - keep, keep);
        vio_tail_len = keep;
    }
    memcpy(vio_tail + vio_tail_len, text, n);
    vio_tail_len += n;
    vio_tail[vio_tail_len++] = '\n';
    vio_tail[vio_tail_len] = '\0';
}

const char *vio_log_tail(void)
{
    return vio_tail;
}

void vio_log(const char *fmt, ...)
{
    char text[512];
    va_list args;
    int length;

    va_start(args, fmt);
    length = vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if(length < 0) return;
    if((size_t)length >= sizeof(text)) length = (int)sizeof(text) - 1;

    vio_append(text);

    fputs(text, stderr);
    fputc('\n', stderr);
    fflush(stderr);

    if(kmsg_fd < 0) kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if(kmsg_fd >= 0) {
        char line[560];
        int n = snprintf(line, sizeof(line), "<6>homo: %s\n", text);
        if(n > 0) {
            ssize_t ignored = write(kmsg_fd, line, (size_t)n);
            (void)ignored;
        }
    }
}

int vio_kernel_tail(char *buf, size_t cap)
{
    char chunk[4096];
    int fd, total = 0;

    if(cap == 0) return -1;
    buf[0] = '\0';
    fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if(fd < 0) return -1;
    for(;;) {
        ssize_t r = read(fd, chunk, sizeof(chunk) - 1);
        int n;
        if(r <= 0) break;
        chunk[r] = '\0';
        n = (int)r;
        if((size_t)n >= cap) {
            memmove(buf, chunk + n - (int)cap + 1, cap - 1);
            total = (int)cap - 1;
        } else if(total + n >= (int)cap) {
            int keep = (int)cap / 2;
            memmove(buf, buf + total - keep, (size_t)keep);
            total = keep;
            memcpy(buf + total, chunk, (size_t)n);
            total += n;
        } else {
            memcpy(buf + total, chunk, (size_t)n);
            total += n;
        }
        buf[total] = '\0';
    }
    close(fd);
    return total;
}

int vio_mkdir_p(const char *path, unsigned mode)
{
    char copy[512];
    size_t length = strlen(path);

    if(length == 0 || length >= sizeof(copy)) return -1;
    memcpy(copy, path, length + 1);
    if(copy[length - 1] == '/') copy[length - 1] = '\0';

    for(char *p = copy + 1; *p; p++) {
        if(*p != '/') continue;
        *p = '\0';
        if(mkdir(copy, (mode_t)mode) < 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if(mkdir(copy, (mode_t)mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

int vio_mount_fs(const char *source, const char *target, const char *type,
                 unsigned long flags, const char *data)
{
    if(mount(source, target, type, flags, data) < 0) {
        if(errno == EBUSY) return 0;
        vio_log("mount %s on %s failed: %s", type, target, strerror(errno));
        return -1;
    }
    return 0;
}

int vio_read_file(const char *path, char *buf, size_t cap)
{
    int fd;
    size_t used = 0;

    if(cap == 0) return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    while(used + 1 < cap) {
        ssize_t n = read(fd, buf + used, cap - 1 - used);
        if(n < 0) {
            if(errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if(n == 0) break;
        used += (size_t)n;
    }
    close(fd);
    buf[used] = '\0';
    return (int)used;
}

int vio_read_int(const char *path, int *out)
{
    char text[64];
    char *end = NULL;
    long value;

    if(vio_read_file(path, text, sizeof(text)) < 0) return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if(end == text || errno != 0) return -1;
    if(out) *out = (int)value;
    return 0;
}

int vio_write_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    size_t length, done = 0;

    if(fd < 0) return -1;
    length = strlen(text);
    while(done < length) {
        ssize_t n = write(fd, text + done, length - done);
        if(n < 0) {
            if(errno == EINTR) continue;
            close(fd);
            return -1;
        }
        done += (size_t)n;
    }
    close(fd);
    return 0;
}

uint64_t vio_monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

void vio_sleep_ms(unsigned ms)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(ms / 1000u);
    delay.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while(nanosleep(&delay, &delay) < 0 && errno == EINTR) {
    }
}

static int make_node(const char *path, unsigned major_no, unsigned minor_no,
                     mode_t type)
{
    struct stat st;
    if(stat(path, &st) == 0) return 0;
    if(mknod(path, type | 0666, makedev(major_no, minor_no)) < 0) {
        if(errno == EEXIST) return 0;
        return -1;
    }
    return 0;
}

static int node_from_sys(const char *sysdir, const char *name,
                         const char *devdir, mode_t type)
{
    char dev_path[256], node[320], text[64];
    unsigned major_no, minor_no;
    struct stat st;
    int length;

    length = snprintf(dev_path, sizeof(dev_path), "%s/%s/dev", sysdir, name);
    if(length <= 0 || (size_t)length >= sizeof(dev_path)) return -1;
    if(vio_read_file(dev_path, text, sizeof(text)) < 0) return -1;
    if(sscanf(text, "%u:%u", &major_no, &minor_no) != 2) return -1;

    length = snprintf(node, sizeof(node), "%s/%s", devdir, name);
    if(length <= 0 || (size_t)length >= sizeof(node)) return -1;
    if(stat(node, &st) == 0) {
        /* A node of the WRONG type is worse than no node. Measured
         * 2026-11-09: this function created /dev/mmcblk0p77 as a CHARACTER
         * device, the later S_IFBLK mknod saw stat() succeed and skipped,
         * and every p77 write failed with no diagnostic. */
        if((st.st_mode & S_IFMT) == (type & S_IFMT)) return 0;
        if(unlink(node) < 0) return -1;
    }

    if(mknod(node, type | 0666, makedev(major_no, minor_no)) < 0) {
        if(errno == EEXIST) return 0;
        return -1;
    }
    return 0;
}

static void scan_class(const char *sysdir, const char *prefix,
                       const char *devdir, mode_t type)
{
    DIR *dir = opendir(sysdir);
    struct dirent *entry;

    if(dir == NULL) return;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') continue;
        if(prefix && strncmp(entry->d_name, prefix, strlen(prefix)) != 0) continue;
        /* /sys/class/drm also lists connector names such as card0-DSI-1. */
        if(strchr(entry->d_name, '-') != NULL) continue;
        node_from_sys(sysdir, entry->d_name, devdir, type);
    }
    closedir(dir);
}

int vio_create_devnodes(void)
{
    int made = 0;

    vio_mkdir_p("/dev", 0755);
    vio_mkdir_p("/dev/dri", 0755);
    vio_mkdir_p("/dev/input", 0755);
    vio_mkdir_p("/dev/block", 0755);
    vio_mkdir_p("/dev/snd", 0755);
    vio_mkdir_p("/dev/pts", 0755);

    made += make_node("/dev/null", 1, 3, S_IFCHR) == 0;
    made += make_node("/dev/zero", 1, 5, S_IFCHR) == 0;
    made += make_node("/dev/full", 1, 7, S_IFCHR) == 0;
    made += make_node("/dev/random", 1, 8, S_IFCHR) == 0;
    made += make_node("/dev/urandom", 1, 9, S_IFCHR) == 0;
    made += make_node("/dev/kmsg", 1, 11, S_IFCHR) == 0;
    made += make_node("/dev/console", 5, 1, S_IFCHR) == 0;
    made += make_node("/dev/tty", 5, 0, S_IFCHR) == 0;
    made += make_node("/dev/ptmx", 5, 2, S_IFCHR) == 0;
    made += make_node("/dev/uinput", 10, 223, S_IFCHR) == 0;
    made += make_node("/dev/watchdog", 10, 130, S_IFCHR) == 0;

    scan_class("/sys/class/drm", "card", "/dev/dri", S_IFCHR);
    scan_class("/sys/class/input", "event", "/dev/input", S_IFCHR);
    scan_class("/sys/class/rtc", "rtc", "/dev", S_IFCHR);
    scan_class("/sys/class/block", "mmcblk0", "/dev", S_IFBLK);
    scan_class("/sys/class/block", "sd", "/dev", S_IFBLK);

    /* The rescue channel addresses the recovery partition by its stable name.
     * Verify the node TYPE, not just its existence: a character device with
     * the right major:minor silently rejects every write. */
    {
        struct stat st;
        char text[64];
        unsigned major_no, minor_no;
        int need = 0;

        if(stat("/dev/mmcblk0p77", &st) != 0) need = 1;
        else if(!S_ISBLK(st.st_mode)) {
            unlink("/dev/mmcblk0p77");
            need = 1;
        }
        if(need &&
           vio_read_file("/sys/class/block/mmcblk0p77/dev", text, sizeof(text)) > 0 &&
           sscanf(text, "%u:%u", &major_no, &minor_no) == 2) {
            if(mknod("/dev/mmcblk0p77", S_IFBLK | 0666,
                     makedev(major_no, minor_no)) == 0) made++;
        }
        if(stat("/dev/mmcblk0p77", &st) == 0)
            vio_log("dev: /dev/mmcblk0p77 %s",
                    S_ISBLK(st.st_mode) ? "is a block device" :
                                          "is NOT a block device");
        else
            vio_log("dev: /dev/mmcblk0p77 missing - p77 writes will fail");
    }
    return made;
}

int vio_wait_for_path(const char *path, unsigned timeout_ms)
{
    uint64_t deadline = vio_monotonic_ms() + timeout_ms;
    for(;;) {
        if(access(path, F_OK) == 0) return 0;
        if(vio_monotonic_ms() >= deadline) return -1;
        vio_create_devnodes();
        vio_sleep_ms(50);
    }
}

int vio_setup_environment(void)
{
    vio_mkdir_p("/proc", 0755);
    vio_mkdir_p("/sys", 0755);
    vio_mkdir_p("/dev", 0755);
    vio_mkdir_p("/run", 0755);
    vio_mkdir_p("/tmp", 01777);
    vio_mkdir_p("/mnt", 0755);
    vio_mkdir_p("/config", 0755);
    vio_mkdir_p("/var/lib/hachimi", 0755);

    vio_mount_fs("proc", "/proc", "proc", 0, "");
    vio_mount_fs("sysfs", "/sys", "sysfs", 0, "");
    /* 32 MiB LUN image plus room for the rest of the runtime state. */
    vio_mount_fs("tmpfs", "/dev", "tmpfs", 0, "mode=0755,size=16m");
    vio_mount_fs("tmpfs", "/run", "tmpfs", 0, "mode=0755,size=128m");
    vio_mount_fs("tmpfs", "/tmp", "tmpfs", 0, "mode=1777,size=32m");
    /* configfs gets its own top-level mountpoint. /sys/kernel/config only
     * exists on kernels that pre-create it in sysfs; a plain initramfs cannot
     * mkdir inside sysfs, so mounting under /config is the portable choice. */
    vio_mount_fs("configfs", "/config", "configfs", 0, "");

    vio_create_devnodes();
    return 0;
}

static int write_brightness(const char *dir, int percent)
{
    char path[256];
    char text[32];
    int maximum = 255;

    if(snprintf(path, sizeof(path), "%s/max_brightness", dir) < (int)sizeof(path))
        (void)vio_read_int(path, &maximum);
    if(maximum <= 0) maximum = 255;

    if(percent < 1) percent = 1;
    if(percent > 100) percent = 100;
    snprintf(text, sizeof(text), "%d\n", maximum * percent / 100);
    if(snprintf(path, sizeof(path), "%s/brightness", dir) >= (int)sizeof(path)) return -1;
    return vio_write_file(path, text);
}

int vio_set_backlight(int percent)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir("/sys/class/backlight");
    if(dir != NULL) {
        while((entry = readdir(dir)) != NULL) {
            char path[256];
            if(entry->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), "/sys/class/backlight/%s", entry->d_name);
            if(write_brightness(path, percent) == 0) {
                vio_log("backlight: %s at %d%%", path, percent);
                closedir(dir);
                return 0;
            }
        }
        closedir(dir);
    }

    dir = opendir("/sys/class/leds");
    if(dir != NULL) {
        while((entry = readdir(dir)) != NULL) {
            char path[256];
            if(entry->d_name[0] == '.') continue;
            if(strstr(entry->d_name, "lcd") == NULL &&
               strstr(entry->d_name, "backlight") == NULL &&
               strstr(entry->d_name, "wl") == NULL) continue;
            snprintf(path, sizeof(path), "/sys/class/leds/%s", entry->d_name);
            if(write_brightness(path, percent) == 0) {
                vio_log("backlight: %s at %d%%", path, percent);
                closedir(dir);
                return 0;
            }
        }
        closedir(dir);
    }
    return -1;
}

static int has_axis(const unsigned char *bits, int axis)
{
    return (bits[axis / 8] >> (axis % 8)) & 1;
}

const char *vio_find_touch_device(char *buf, size_t cap)
{
    DIR *dir = opendir("/dev/input");
    struct dirent *entry;
    char fallback[128] = {0};

    if(dir == NULL) return NULL;
    while((entry = readdir(dir)) != NULL) {
        unsigned char abs_bits[ABS_CNT / 8 + 1];
        char path[320], name[128] = {0};
        int fd;

        if(strncmp(entry->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) continue;

        memset(abs_bits, 0, sizeof(abs_bits));
        if(ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0 &&
           has_axis(abs_bits, ABS_MT_POSITION_X) &&
           has_axis(abs_bits, ABS_MT_POSITION_Y)) {
            (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            close(fd);
            if(strstr(name, "touch") || strstr(name, "Touch") ||
               strstr(name, "fts") || strstr(name, "nt36") ||
               strstr(name, "NVT") || strstr(name, "Goodix")) {
                snprintf(buf, cap, "%s", path);
                vio_log("touch: using %s (%s)", path, name);
                closedir(dir);
                return buf;
            }
            if(fallback[0] == '\0') snprintf(fallback, sizeof(fallback), "%s", path);
            continue;
        }
        close(fd);
    }
    closedir(dir);

    if(fallback[0]) {
        snprintf(buf, cap, "%s", fallback);
        vio_log("touch: using fallback %s", fallback);
        return buf;
    }
    return NULL;
}
