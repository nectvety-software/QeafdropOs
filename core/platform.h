/* SPDX-License-Identifier: MIT */
/* core/platform.h - feature platform: apps, permissions, sandbox, adaptive
 * power, theft protection, connectivity status and the AI intent seam.
 *
 * Honesty contract: everything here is real state with real decisions, but
 * there is NO on-device ML model, NO NFC/UWB hardware on violet, and NO way
 * to load third-party binary code. Where hardware or a host service is
 * required, the API reports "unavailable" instead of faking a success.
 * The "AI" surface is an intent queue that VQEAF Tool can service over the
 * rescue channel; the rule-based adaptive policy is explicitly labelled as
 * a heuristic, not a neural network.
 */

#ifndef UMOS_PLATFORM_H
#define UMOS_PLATFORM_H

#include <stdint.h>

/* ---- app registry & launcher ---------------------------------------------- */
#define PLATFORM_MAX_APPS 12

enum platform_app_flags {
    APP_FLAG_LAUNCHER_CAPABLE = 1u << 0,
    APP_FLAG_THIRD_PARTY     = 1u << 1,
    APP_FLAG_SYSTEM          = 1u << 2
};

struct app_info {
    int id;
    const char *name;
    int icon_id;
    uint32_t flags;
};

void platform_apps_init(void);
int  platform_app_count(void);
const struct app_info *platform_app(int index);
const struct app_info *platform_app_by_id(int id);
/* Selects the active launcher. Returns -1 for an unknown / incapable id.
 * This is a REGISTRY seam: the OS does not load external binaries. */
int  platform_set_launcher(int app_id);
int  platform_launcher(void);
int  platform_register_third_party_launcher(const char *name);

/* ---- permissions ----------------------------------------------------------- */
enum platform_perm {
    PERM_CAMERA = 0,
    PERM_MIC,
    PERM_LOCATION,
    PERM_COUNT
};

enum perm_mode {
    PERM_DENY = 0,
    PERM_ONE_TIME,
    PERM_WHILE_IN_USE,
    PERM_ALWAYS,
    PERM_MODE_COUNT
};

int  platform_grant(int app_id, enum platform_perm p, enum perm_mode m);
enum perm_mode platform_perm_mode(int app_id, enum platform_perm p);
/* foreground=1 means the app is on screen. Returns 1 when access is allowed.
 * A ONE_TIME grant is consumed by the first allowed call. Any access while
 * backgrounded raises an alert that the UI can surface. */
int  platform_check(int app_id, enum platform_perm p, int foreground);
int  platform_alerts(void);
void platform_clear_alerts(void);
const char *platform_perm_name(enum platform_perm p);
const char *platform_perm_mode_name(enum perm_mode m);

/* ---- sandbox --------------------------------------------------------------- */
/* Returns 1 when `path` belongs to app_id's own namespace. Cross-app paths
 * and traversal attempts are refused. */
int  platform_sandbox_check(int app_id, const char *path);
int  platform_sandbox_denials(void);

/* ---- adaptive battery / RAM (deterministic heuristic, not ML) ------------- */
#define ADAPTIVE_MAX_APPS PLATFORM_MAX_APPS

void platform_record_usage(int app_id, uint32_t foreground_ms,
                           uint32_t background_wakes);
/* now_min is a monotonic minute counter; low_battery and charging come from
 * the battery HAL. Freezes apps that are rarely used but wake often. */
void platform_adaptive_tick(int now_min, int low_battery, int charging);
int  platform_frozen(int app_id);
void platform_unfreeze(int app_id);
int  platform_frozen_count(void);
uint32_t platform_usage_score(int app_id);
int  platform_adaptive_is_ml(void);       /* always 0: it is a rule engine */

/* ---- theft protection ------------------------------------------------------ */
/* Gravity in milli-g. A snatch produces a large short-lived delta. */
void platform_feed_motion(int gx, int gy, int gz);
int  platform_theft_tick(void);           /* 1 when it just locked */
int  platform_locked(void);
void platform_unlock(void);
int  platform_theft_alerts(void);

/* ---- connectivity ---------------------------------------------------------- */
#define CONN_WIFI   (1u << 0)
#define CONN_BT     (1u << 1)
#define CONN_NFC    (1u << 2)
#define CONN_UWB    (1u << 3)
#define CONN_USB    (1u << 4)

/* Reads the HAL seams that exist. NFC/UWB report 0 on violet: no hardware. */
uint32_t platform_connectivity(void);
int  platform_nfc_available(void);
int  platform_uwb_available(void);

/* ---- AI assistant seam ----------------------------------------------------- */
#define AI_INTENT_MAX 8
#define AI_INTENT_LEN 64

/* 0: there is no model bundled in this image. The host (VQEAF Tool) may pull
 * queued intents over the rescue channel and answer them with its own LLM. */
int  platform_ai_available(void);
int  platform_ai_enqueue(const char *intent);
/* Copies the oldest pending intent into out; returns its length or -1. */
int  platform_ai_pull(char *out, int cap);
int  platform_ai_pending(void);

#endif
