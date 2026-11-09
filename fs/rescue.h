/* SPDX-License-Identifier: MIT */
/* fs/rescue.h - QEAF.CMD command loop over the RAM FAT16 LUN. */

#ifndef UMOS_RESCUE_H
#define UMOS_RESCUE_H

#include "fs/fat16.h"

/* Publish HELP.TXT / SUMMARY.TXT / RESPONSE.TXT / QEAFBOOT.LOG. */
void rescue_init(struct fat16_volume *vol);

/* Called once per main-loop tick. Runs a QEAF.CMD batch once (dedup by
 * content) and writes the answer back to RESPONSE.TXT. */
void rescue_poll(struct fat16_volume *vol);

/* Host-gate seam: point the flash verb at a fixture file instead of p77. */
void rescue_set_partition_path(const char *path);

/* Optional screen dump. The platform registers a function that reads the
 * live scanout buffer, downscales it to RGB565 and writes it into `vol` as
 * SCREEN.RAW. Returns the byte count, or a negative error. Without a hook
 * the `snap` verb answers "unavailable" and nothing is written. */
#define RESCUE_SCREEN_W 240
#define RESCUE_SCREEN_H 520
#define RESCUE_SCREEN_BYTES (RESCUE_SCREEN_W * RESCUE_SCREEN_H * 2)
typedef int (*rescue_snapshot_fn)(struct fat16_volume *vol);
void rescue_set_snapshot_hook(rescue_snapshot_fn fn);

/* Optional UI language switch. The platform registers a function that applies
 * a core/i18n language value to the running UI and persists it. Returns 0 when
 * the language was applied, a negative error otherwise. Without a hook the
 * `lang` verb answers "unavailable". */
typedef int (*rescue_language_fn)(int lang);
void rescue_set_language_hook(rescue_language_fn fn);

/* 1 = last flash verified, negative = refused/failed, 0 = no flash yet. */
int rescue_last_result(void);

#endif
