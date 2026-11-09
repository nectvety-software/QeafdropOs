/* platform/violet/violet_power.c - KEY_POWER + KEY_VOLUMEUP/DOWN. */
#define _GNU_SOURCE
#include "violet_power.h"
#include "violet.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int vio_power_open(struct vio_power *pw, const char *path) {
    memset(pw, 0, sizeof(*pw));
    for (int i = 0; i < VIO_POWER_FDS; i++) pw->fds[i] = -1;
    pw->nfd = 0;
    pw->volume = 25;
    if (!path) return -1;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        vio_log("power: cannot open %s: %s", path, strerror(errno));
        return -1;
    }
    pw->fds[0] = fd;
    pw->nfd = 1;
    return 0;
}

int vio_power_open_more(struct vio_power *pw, const char *path);

int vio_power_open_more(struct vio_power *pw, const char *path) {
    if (!pw || pw->nfd >= VIO_POWER_FDS) return -1;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    pw->fds[pw->nfd++] = fd;
    return 0;
}

const char *vio_find_power_device(char *buf, int cap) {
    (void)buf; (void)cap;
    return buf;
}

int vio_power_poll(struct vio_power *pw) {
    struct input_event ev;
    ssize_t n;
    pw->volume_delta = 0;
    if (!pw || pw->nfd <= 0) return 0;
    for (int k = 0; k < pw->nfd; k++) {
        int fd = pw->fds[k];
        if (fd < 0) continue;
        while ((n = read(fd, &ev, sizeof(ev))) > 0) {
            if (ev.type == EV_KEY && ev.code == KEY_POWER) {
                if (ev.value == 1) {
                    pw->pressed = 1;
                    pw->pressed_ever = 1;
                    pw->pressed_since_ms = vio_monotonic_ms();
                    pw->menu_shown = 0;
                } else if (ev.value == 0) {
                    pw->pressed = 0;
                }
            } else if (ev.type == EV_KEY && (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN)) {
                if (ev.value == 1) {
                    int d = (ev.code == KEY_VOLUMEUP) ? 10 : -10;
                    pw->volume += d;
                    if (pw->volume < 0) pw->volume = 0;
                    if (pw->volume > 100) pw->volume = 100;
                    pw->volume_delta += d;
                }
            }
        }
    }
    if (pw->pressed && !pw->menu_shown && pw->pressed_ever) {
        uint64_t el = vio_monotonic_ms() - pw->pressed_since_ms;
        if (el >= 1000) {
            pw->menu_shown = 1;
            return 1;
        }
    }
    return 0;
}

int vio_power_volume(struct vio_power *pw) {
    return pw ? pw->volume : 0;
}
