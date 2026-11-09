/* SPDX-License-Identifier: MIT */
/* hal/include/hal.h - UMOS Hardware Abstraction Layer */

#ifndef UMOS_HAL_H
#define UMOS_HAL_H

#include <stdint.h>

#define UMOS_OK 0
#define UMOS_ERR -1
#define UMOS_NOENT -2
#define UMOS_NOMEM -3

struct hal_display {
    int (*init)(void);
    int (*get_geometry)(uint32_t *w, uint32_t *h, uint32_t *stride);
    uint16_t *(*fb)(void);
    int (*flush)(int x, int y, int w, int h);
};

struct hal_touch {
    int (*init)(void);
    int (*poll)(int *x, int *y, int *pressed);
};

struct hal_audio {
    int (*init)(void);
    int (*beep)(uint16_t freq_hz, uint32_t ms);
};

struct hal_storage {
    int (*init)(void);
    int (*read_sector)(uint32_t lba, uint8_t *buf);
    int (*write_sector)(uint32_t lba, const uint8_t *buf);
    uint32_t (*total_sectors)(void);
};

struct hal_power {
    int (*init)(void);
    int (*reboot)(void);
    int (*poweroff)(void);
};

struct hal_rtc {
    int (*init)(void);
    uint64_t (*now_ms)(void);
};

struct hal_radio {
    int (*init)(void);
    int (*wifi_on)(void);
    int (*wifi_off)(void);
};

extern int hal_init(void);
extern int hal_screen_w(void);
extern int hal_screen_h(void);
extern int hal_stride(void);
extern uint16_t *hal_fb(void);
extern struct hal_rtc *hal_rtc(void);

void hal_register_display(struct hal_display *d);
void hal_register_touch(struct hal_touch *t);
void hal_register_audio(struct hal_audio *a);
void hal_register_storage(struct hal_storage *s);
void hal_register_power(struct hal_power *p);
void hal_register_rtc(struct hal_rtc *r);
void hal_register_radio(struct hal_radio *r);

struct hal_display *hal_display(void);
struct hal_touch *hal_touch(void);
struct hal_audio *hal_audio(void);
struct hal_storage *hal_storage(void);
struct hal_power *hal_power(void);
struct hal_rtc *hal_rtc(void);
struct hal_radio *hal_radio(void);

#endif
