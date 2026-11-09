/* SPDX-License-Identifier: MIT */
/* core/platform.c - real state machines behind the feature list.
 *
 * No dynamic allocation, no libc string-heavy deps beyond what libc_stubs.c
 * supplies, no floats. Every getter in platform.h reads this state, so the
 * Features page and the host gate can only report what actually happened.
 */

#include "core/platform.h"
#include "gui/gfx/theme.h"
#include "hal/include/usb_gadget.h"
#include "hal/include/wifi.h"
#include "hal/include/bluetooth.h"

#include <stddef.h>
#include <stdint.h>

/* The freestanding link has libc_stubs.c, not libc headers. Declare just the
 * two string primitives this file uses instead of pulling in <string.h>. */
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strstr(const char *hay, const char *needle);

/* ---- app registry ---------------------------------------------------------- */
static struct app_info g_apps[PLATFORM_MAX_APPS];
static int g_napps;
static int g_launcher = -1;

static const char *const APP_NAMES[8] = {
    "Home", "Lock", "Settings", "Messages",
    "Gallery", "Browser", "Recovery", "Fastboot"
};

void platform_apps_init(void) {
    static const uint32_t flags[8] = {
        APP_FLAG_LAUNCHER_CAPABLE | APP_FLAG_SYSTEM,
        APP_FLAG_SYSTEM, APP_FLAG_SYSTEM, APP_FLAG_SYSTEM,
        APP_FLAG_SYSTEM, APP_FLAG_SYSTEM, APP_FLAG_SYSTEM, APP_FLAG_SYSTEM
    };
    g_napps = 0;
    for (int i = 0; i < 8; i++) {
        g_apps[g_napps].id = i;
        g_apps[g_napps].name = APP_NAMES[i];
        g_apps[g_napps].icon_id = i;
        g_apps[g_napps].flags = flags[i];
        g_napps++;
    }
    g_launcher = 0;
}

int platform_app_count(void) { return g_napps; }

const struct app_info *platform_app(int index) {
    if (index < 0 || index >= g_napps) return NULL;
    return &g_apps[index];
}

const struct app_info *platform_app_by_id(int id) {
    for (int i = 0; i < g_napps; i++) if (g_apps[i].id == id) return &g_apps[i];
    return NULL;
}

int platform_set_launcher(int app_id) {
    const struct app_info *a = platform_app_by_id(app_id);
    if (!a || !(a->flags & APP_FLAG_LAUNCHER_CAPABLE)) return -1;
    g_launcher = app_id;
    return 0;
}

int platform_launcher(void) { return g_launcher; }

int platform_register_third_party_launcher(const char *name) {
    if (!name || !name[0] || g_napps >= PLATFORM_MAX_APPS) return -1;
    g_apps[g_napps].id = 100 + g_napps;
    g_apps[g_napps].name = name;
    g_apps[g_napps].icon_id = theme_icon_pack();
    g_apps[g_napps].flags = APP_FLAG_LAUNCHER_CAPABLE | APP_FLAG_THIRD_PARTY;
    return g_apps[g_napps++].id;
}

/* ---- permissions ----------------------------------------------------------- */
static enum perm_mode g_perms[PLATFORM_MAX_APPS][PERM_COUNT];
static int g_alerts;

static const char *const PERM_NAMES[PERM_COUNT] = {
    "camera", "microphone", "location"
};
static const char *const MODE_NAMES[PERM_MODE_COUNT] = {
    "deny", "only this time", "while in use", "always"
};

const char *platform_perm_name(enum platform_perm p) {
    if (p < 0 || p >= PERM_COUNT) return "unknown";
    return PERM_NAMES[p];
}
const char *platform_perm_mode_name(enum perm_mode m) {
    if (m < 0 || m >= PERM_MODE_COUNT) return "unknown";
    return MODE_NAMES[m];
}

static int app_index(int app_id) {
    for (int i = 0; i < g_napps; i++) if (g_apps[i].id == app_id) return i;
    return -1;
}

int platform_grant(int app_id, enum platform_perm p, enum perm_mode m) {
    int i = app_index(app_id);
    if (i < 0 || p < 0 || p >= PERM_COUNT || m < 0 || m >= PERM_MODE_COUNT)
        return -1;
    g_perms[i][p] = m;
    return 0;
}

enum perm_mode platform_perm_mode(int app_id, enum platform_perm p) {
    int i = app_index(app_id);
    if (i < 0 || p < 0 || p >= PERM_COUNT) return PERM_DENY;
    return g_perms[i][p];
}

int platform_check(int app_id, enum platform_perm p, int foreground) {
    int i = app_index(app_id);
    enum perm_mode m;
    if (i < 0 || p < 0 || p >= PERM_COUNT) return 0;
    m = g_perms[i][p];
    if (!foreground && (m == PERM_ALWAYS || m == PERM_ONE_TIME)) {
        /* Background collection is exactly what the privacy warning is for. */
        g_alerts++;
    }
    if (m == PERM_DENY) return 0;
    if (m == PERM_WHILE_IN_USE && !foreground) return 0;
    if (m == PERM_ONE_TIME) {
        g_perms[i][p] = PERM_DENY;   /* consume the one-time grant */
        return 1;
    }
    return 1;
}

int platform_alerts(void) { return g_alerts; }
void platform_clear_alerts(void) { g_alerts = 0; }

/* ---- sandbox --------------------------------------------------------------- */
static int g_sandbox_denials;

int platform_sandbox_check(int app_id, const char *path) {
    char prefix[16];
    int n = 0;
    const char *p;

    if (!path || app_index(app_id) < 0) { g_sandbox_denials++; return 0; }
    if (path[0] == '.' || strstr(path, "..") != NULL) {
        g_sandbox_denials++; return 0;
    }
    /* Own namespace is /data/app/<id>/... ; anything else is another app's
     * data or a system path and is refused for a normal app. */
    prefix[n++] = '/';
    p = "data/app/";
    while (*p && n + 1 < (int)sizeof(prefix)) prefix[n++] = *p++;
    {
        int v = app_id, t = 0;
        char num[12];
        do { num[t++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (t > 0 && n + 1 < (int)sizeof(prefix)) prefix[n++] = num[--t];
    }
    prefix[n] = '\0';
    if (strncmp(path, prefix, (size_t)n) != 0 || path[n] != '/') {
        g_sandbox_denials++;
        return 0;
    }
    return 1;
}

int platform_sandbox_denials(void) { return g_sandbox_denials; }

/* ---- adaptive battery / RAM ------------------------------------------------ */
struct usage {
    uint32_t launches;
    uint32_t foreground_ms;
    uint32_t background_wakes;
    int last_used_min;
    int frozen;
    uint32_t score;
};
static struct usage g_usage[ADAPTIVE_MAX_APPS];

void platform_record_usage(int app_id, uint32_t foreground_ms,
                           uint32_t background_wakes) {
    int i = app_index(app_id);
    if (i < 0) return;
    g_usage[i].foreground_ms += foreground_ms;
    g_usage[i].background_wakes += background_wakes;
    if (foreground_ms) g_usage[i].launches++;
}

uint32_t platform_usage_score(int app_id) {
    int i = app_index(app_id);
    if (i < 0) return 0;
    return g_usage[i].score;
}

void platform_adaptive_tick(int now_min, int low_battery, int charging) {
    for (int i = 0; i < g_napps; i++) {
        struct usage *u = &g_usage[i];
        u->score = u->launches * 100u + u->foreground_ms / 1000u;
        if (u->foreground_ms) u->last_used_min = now_min;
        if (u->frozen) continue;
        /* A deterministic rule: rarely used, wakes often, stale, and the
         * battery is low. Explicitly NOT a learned model. */
        if (low_battery && !charging && u->launches <= 1 &&
            u->background_wakes >= 3 &&
            (now_min - u->last_used_min) >= 30) {
            u->frozen = 1;
        }
    }
}

int platform_frozen(int app_id) {
    int i = app_index(app_id);
    return i < 0 ? 0 : g_usage[i].frozen;
}

void platform_unfreeze(int app_id) {
    int i = app_index(app_id);
    if (i >= 0) g_usage[i].frozen = 0;
}

int platform_frozen_count(void) {
    int n = 0;
    for (int i = 0; i < g_napps; i++) if (g_usage[i].frozen) n++;
    return n;
}

int platform_adaptive_is_ml(void) { return 0; }

/* ---- theft protection ------------------------------------------------------ */
static int g_last_gx, g_last_gy, g_last_gz;
static int g_have_motion;
static int g_locked;
static int g_theft_alerts;
#define THEFT_DELTA_MG 1400

void platform_feed_motion(int gx, int gy, int gz) {
    g_last_gx = gx; g_last_gy = gy; g_last_gz = gz;
    g_have_motion = 1;
}

static int iabs(int v) { return v < 0 ? -v : v; }

int platform_theft_tick(void) {
    int delta;
    if (!g_have_motion || g_locked) return 0;
    delta = iabs(g_last_gx) + iabs(g_last_gy) + iabs(g_last_gz);
    g_have_motion = 0;
    if (delta < THEFT_DELTA_MG) return 0;
    g_locked = 1;
    g_theft_alerts++;
    return 1;
}

int platform_locked(void) { return g_locked; }
void platform_unlock(void) { g_locked = 0; }
int platform_theft_alerts(void) { return g_theft_alerts; }

/* ---- connectivity ---------------------------------------------------------- */
uint32_t platform_connectivity(void) {
    uint32_t bits = 0;
    /* Only report a radio whose HAL actually says it is up. */
    if (wifi_status() == WIFI_STATUS_CONNECTED) bits |= CONN_WIFI;
    if (bt_status() == BT_STATUS_ON || bt_status() == BT_STATUS_CONNECTED)
        bits |= CONN_BT;
    /* violet has neither NFC nor UWB: those bits are never set. */
    if (usb_gadget_cable()) bits |= CONN_USB;
    return bits;
}

int platform_nfc_available(void) { return 0; }
int platform_uwb_available(void) { return 0; }

/* ---- AI assistant seam ----------------------------------------------------- */
static char g_ai[AI_INTENT_MAX][AI_INTENT_LEN];
static int g_ai_head, g_ai_tail;

int platform_ai_available(void) { return 0; }   /* no on-device model */

int platform_ai_enqueue(const char *intent) {
    int next, n = 0;
    if (!intent) return -1;
    next = (g_ai_head + 1) % AI_INTENT_MAX;
    if (next == g_ai_tail) return -1;
    while (intent[n] && n < AI_INTENT_LEN - 1) {
        g_ai[g_ai_head][n] = intent[n]; n++;
    }
    g_ai[g_ai_head][n] = '\0';
    g_ai_head = next;
    return n;
}

int platform_ai_pull(char *out, int cap) {
    int n = 0;
    if (!out || cap <= 0 || g_ai_tail == g_ai_head) return -1;
    while (g_ai[g_ai_tail][n] && n < cap - 1) {
        out[n] = g_ai[g_ai_tail][n]; n++;
    }
    out[n] = '\0';
    g_ai_tail = (g_ai_tail + 1) % AI_INTENT_MAX;
    return n;
}

int platform_ai_pending(void) {
    return (g_ai_head - g_ai_tail + AI_INTENT_MAX) % AI_INTENT_MAX;
}
