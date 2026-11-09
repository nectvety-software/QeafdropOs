/* SPDX-License-Identifier: MIT */
/* platform/violet/violet_input.c - evdev touch reader.
 *
 * The FTS / NT36xxx drivers report ABS_MT_POSITION_X/Y plus BTN_TOUCH and
 * ABS_MT_TRACKING_ID. Slot 0 is the pointer; extra fingers are ignored so a
 * two-finger gesture cannot make the cursor jump between slots.
 */
#define _GNU_SOURCE

#include "violet_input.h"

#include "violet.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/input.h>

static int axis_present(const unsigned char *bits, int axis)
{
    return (bits[axis / 8] >> (axis % 8)) & 1;
}

static void read_range(int fd, int axis, int *minimum, int *maximum)
{
    struct input_absinfo info;
    if(ioctl(fd, EVIOCGABS(axis), &info) == 0 && info.maximum > info.minimum) {
        *minimum = info.minimum;
        *maximum = info.maximum;
    }
}

int vio_touch_open(struct vio_touch *touch, const char *path, vio_panel_t *panel)
{
    unsigned char abs_bits[ABS_CNT / 8 + 1];

    memset(touch, 0, sizeof(*touch));
    touch->fd = -1;
    touch->panel = panel;
    touch->min_x = 0;
    touch->max_x = panel->panel_w - 1;
    touch->min_y = 0;
    touch->max_y = panel->panel_h - 1;

    if(path == NULL) return -1;
    touch->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if(touch->fd < 0) {
        vio_log("touch: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    memset(abs_bits, 0, sizeof(abs_bits));
    if(ioctl(touch->fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0) {
        if(axis_present(abs_bits, ABS_MT_POSITION_X) &&
           axis_present(abs_bits, ABS_MT_POSITION_Y)) {
            touch->use_mt = 1;
            read_range(touch->fd, ABS_MT_POSITION_X, &touch->min_x, &touch->max_x);
            read_range(touch->fd, ABS_MT_POSITION_Y, &touch->min_y, &touch->max_y);
        }
        else if(axis_present(abs_bits, ABS_X) && axis_present(abs_bits, ABS_Y)) {
            touch->use_mt = 0;
            read_range(touch->fd, ABS_X, &touch->min_x, &touch->max_x);
            read_range(touch->fd, ABS_Y, &touch->min_y, &touch->max_y);
        }
    }
    vio_log("touch: %s x[%d..%d] y[%d..%d] %s", path,
            touch->min_x, touch->max_x, touch->min_y, touch->max_y,
            touch->use_mt ? "multitouch" : "single-touch");
    return 0;
}

void vio_touch_close(struct vio_touch *touch)
{
    if(touch->fd >= 0) close(touch->fd);
    touch->fd = -1;
}

static void commit(struct vio_touch *touch)
{
    if(touch->pending_pressed && touch->have_x && touch->have_y) {
        int ui_x = 0, ui_y = 0;
        int raw_x = touch->pending_x - touch->min_x;
        int raw_y = touch->pending_y - touch->min_y;
        int raw_w = touch->max_x - touch->min_x + 1;
        int raw_h = touch->max_y - touch->min_y + 1;
        if(vio_panel_touch_map(touch->panel, raw_x, raw_y, raw_w, raw_h,
                               &ui_x, &ui_y) == 0) {
            touch->x = ui_x;
            touch->y = ui_y;
            touch->pressed = 1;
        }
        else {
            /* A tap in the letterbox is not a tap on the desktop. */
            touch->pressed = 0;
        }
    }
    else if(!touch->pending_pressed) {
        touch->pressed = 0;
    }
}

void vio_touch_poll(struct vio_touch *touch)
{
    struct input_event events[32];
    ssize_t count;

    if(touch->fd < 0) return;
    while((count = read(touch->fd, events, sizeof(events))) > 0) {
        size_t n = (size_t)count / sizeof(events[0]);
        for(size_t i = 0; i < n; i++) {
            const struct input_event *ev = &events[i];
            if(ev->type == EV_ABS) {
                if(ev->code == ABS_MT_SLOT) {
                    touch->slot = ev->value;
                }
                else if(touch->slot != 0) {
                    /* Finger 1..N: keep the first contact as the pointer. */
                }
                else if(ev->code == ABS_MT_POSITION_X ||
                        (ev->code == ABS_X && !touch->use_mt)) {
                    touch->pending_x = ev->value;
                    touch->have_x = 1;
                }
                else if(ev->code == ABS_MT_POSITION_Y ||
                        (ev->code == ABS_Y && !touch->use_mt)) {
                    touch->pending_y = ev->value;
                    touch->have_y = 1;
                }
                else if(ev->code == ABS_MT_TRACKING_ID) {
                    touch->pending_pressed = ev->value >= 0;
                }
            }
            else if(ev->type == EV_KEY && ev->code == BTN_TOUCH) {
                touch->pending_pressed = ev->value != 0;
            }
            else if(ev->type == EV_SYN && ev->code == SYN_REPORT) {
                commit(touch);
            }
        }
    }
}
