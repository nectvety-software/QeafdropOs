/* SPDX-License-Identifier: MIT */
/* core/clock.h - the time the system bar shows.
 *
 * The RTC is read from /sys/class/rtc/rtc0/since_epoch. When that node does
 * not exist (or the kernel reports an implausible epoch) the clock falls back
 * to time since boot, and clock_is_wall() says which one answered, so a screen
 * can label it instead of passing uptime off as the time of day.
 */

#ifndef UMOS_CLOCK_H
#define UMOS_CLOCK_H

/* "HH:MM" into out (needs 6 bytes). Safe to call every frame: the answer is
 * cached for one second, so the status bar does not walk sysfs 60 times a
 * second. */
void clock_hhmm(char *out, int cap);

/* 1 when the last answer came from the RTC, 0 when it is uptime. */
int clock_is_wall(void);

#endif
