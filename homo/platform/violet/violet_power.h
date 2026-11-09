#ifndef HOMO_VIOLET_POWER_H
#define HOMO_VIOLET_POWER_H

#include <stdint.h>

#define VIO_POWER_FDS 4

struct vio_power {
    int fds[VIO_POWER_FDS];
    int nfd;
    int pressed;
    uint64_t pressed_since_ms;
    int menu_shown;
    int pressed_ever;
    int volume;
    int volume_delta;
};

int vio_power_open(struct vio_power *pw, const char *path);
int vio_power_open_more(struct vio_power *pw, const char *path);
const char *vio_find_power_device(char *buf, int cap);
int vio_power_poll(struct vio_power *pw);
int vio_power_volume(struct vio_power *pw);

#endif
