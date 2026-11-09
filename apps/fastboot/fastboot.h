/* SPDX-License-Identifier: MIT */
/* apps/fastboot/fastboot.h - VQEAF OS "Fastboot Mode" screen.
 *
 * Poster layout: small brand bar, big V mark, wordmark + tagline, device/build
 * card, a highlighted Continue pill and five plain menu rows, then the volume
 * hint over the teal wave footer. Like recovery, this screen only QUEUES
 * intents; it never reboots, wipes or writes a partition by itself.
 *
 * "Flash Recovery" writes to the p77 partition, so it is destructive and goes
 * through bui_confirm() with CANCEL as the default selection. The consent is
 * an intent only: the actual write is still the board / rescue shell's job.
 */

#ifndef UMOS_FASTBOOT_H
#define UMOS_FASTBOOT_H

#include <stdint.h>

enum fastboot_action {
    FASTBOOT_ACT_NONE = 0,
    FASTBOOT_ACT_CONTINUE,
    FASTBOOT_ACT_RECOVERY,
    FASTBOOT_ACT_UPDATE,
    FASTBOOT_ACT_FACTORY_RESET,
    FASTBOOT_ACT_POWER_OFF,
    FASTBOOT_ACT_FLASH_RECOVERY,    /* writes p77 -> always confirmed */
    FASTBOOT_ACT_COUNT
};

struct fastboot_info {
    const char *device;
    const char *model;
    const char *version;
    const char *build;
    const char *date;
};

void fastboot_init(void);
void fastboot_open(void);
void fastboot_close(void);
int  fastboot_visible(void);
void fastboot_draw(void);
/* Uses the bui_event values declared in apps/bootui/bootui.h. */
int  fastboot_event(uint32_t type, uint32_t d1, uint32_t d2);
void fastboot_poll_input(void);

void fastboot_set_info(const struct fastboot_info *info);
void fastboot_set_status(const char *text);

enum fastboot_action fastboot_take_action(void);
const char *fastboot_action_name(enum fastboot_action a);
int fastboot_action_count(void);
int fastboot_selected(void);
int fastboot_confirming(void);

#endif
