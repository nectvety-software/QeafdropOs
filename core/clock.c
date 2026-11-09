/* SPDX-License-Identifier: MIT */
/* core/clock.c - RTC wall clock, uptime fallback; see core/clock.h. */

#include "core/clock.h"
#include "core/sysio.h"

#define CLOCK_RTC_PATH "/sys/class/rtc/rtc0/since_epoch"
/* Anything before 2020 or after 2100 is a cold RTC, not a date worth showing. */
#define CLOCK_EPOCH_MIN 1577836800
#define CLOCK_EPOCH_MAX 4102444800u

static int g_hh, g_mm;
static int g_wall;
static int g_cached;
static uint64_t g_cache_ms;

static void put_two(char *out, int v) {
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
}

static void refresh(void) {
    uint64_t now = sysio_monotonic_ms();
    int epoch = 0;
    unsigned secs;

    if (g_cached && now - g_cache_ms < 1000ULL) return;
    g_cache_ms = now;
    g_cached = 1;

    /* sysio_read_int answers -1 when the node is missing; a board without an
     * RTC must not turn that error into 00:00 labelled as wall time. */
    if (sysio_read_int(CLOCK_RTC_PATH, &epoch) == 0 &&
        epoch >= (int)CLOCK_EPOCH_MIN && (unsigned)epoch <= CLOCK_EPOCH_MAX) {
        g_wall = 1;
        secs = (unsigned)epoch;
    } else {
        g_wall = 0;
        secs = (unsigned)(now / 1000ULL);
    }
    g_hh = (int)((secs / 3600u) % 24u);
    g_mm = (int)((secs / 60u) % 60u);
}

void clock_hhmm(char *out, int cap) {
    if (!out || cap < 6) { if (out && cap > 0) out[0] = '\0'; return; }
    refresh();
    put_two(out, g_hh);
    out[2] = ':';
    put_two(out + 3, g_mm);
    out[5] = '\0';
}

int clock_is_wall(void) {
    refresh();
    return g_wall;
}
