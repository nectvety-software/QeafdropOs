/* SPDX-License-Identifier: MIT */
/* apps/recovery/recovery.h - Graphical recovery screen for VQEAF OS / violet
 *
 * The GUI never performs a privileged operation itself. It collects an intent,
 * asks for confirmation on destructive ones, and leaves the actual execution to
 * the board / rescue shell through recovery_take_action(). That keeps the only
 * write to p77 behind the existing "flash recovery <FILE83> confirm" guard.
 */

#ifndef UMOS_RECOVERY_H
#define UMOS_RECOVERY_H

#include <stdint.h>

/* Intents handed to the executor. The GUI only requests them. */
enum recovery_action {
    RECOVERY_ACT_NONE = 0,
    RECOVERY_ACT_REBOOT,
    RECOVERY_ACT_BOOTLOADER,
    RECOVERY_ACT_POWEROFF,
    RECOVERY_ACT_FLASH,          /* flash recovery BOOTIMG.BIN confirm */
    RECOVERY_ACT_BOOT_LOG,
    RECOVERY_ACT_DISPLAY_TEST,
    RECOVERY_ACT_RESCUE_STATUS,
    RECOVERY_ACT_BACK,
    RECOVERY_ACT_WIPE_DATA,
    RECOVERY_ACT_WIPE_CACHE,
    RECOVERY_ACT_COUNT
};

/* Input events. Touch carries x in d1 and y in d2 (panel pixels). */
enum recovery_event {
    RECOVERY_EV_UP       = 1,
    RECOVERY_EV_DOWN     = 2,
    RECOVERY_EV_ACTIVATE = 3,
    RECOVERY_EV_TOUCH    = 4,
    RECOVERY_EV_CONFIRM  = 5,
    RECOVERY_EV_CANCEL   = 6
};

/* Strings are borrowed, not copied: pass literals or static buffers. */
struct recovery_info {
    const char *version;
    const char *device;
    const char *slot;
    const char *image;
    const char *usb;
    const char *clock;
    int battery;                 /* 0..100 */
    int signal_bars;             /* 0..4 */
};

void recovery_init(void);
void recovery_open(void);
void recovery_close(void);
int  recovery_visible(void);
void recovery_draw(void);
int  recovery_event(uint32_t type, uint32_t d1, uint32_t d2);
/* Polls hal_touch() once and turns a fresh press into a TOUCH event. Safe to
 * call every frame; does nothing while the board has no touch driver. */
void recovery_poll_input(void);

void recovery_set_info(const struct recovery_info *info);
void recovery_set_status(const char *text);
void recovery_set_progress(int pct);     /* -1 hides the progress bar */

/* Consumes one pending intent. Returns RECOVERY_ACT_NONE when the queue is
 * empty. The executor calls recovery_set_status() / _set_progress() to report
 * back, so the screen never claims an action succeeded on its own. */
enum recovery_action recovery_take_action(void);
const char *recovery_action_name(enum recovery_action a);

/* How many actions are on screen (for host gates and hit testing). */
int recovery_action_count(void);
int recovery_selected(void);
int recovery_confirming(void);

#endif
