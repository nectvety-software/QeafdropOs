/* SPDX-License-Identifier: MIT */
/* core/prefs.h - the small persisted preference file and its p77 slot.
 *
 * /etc/qeaf.conf is a flat "key=value" file, written only when language,
 * orientation or auto-rotate actually changes. It is read once at boot before
 * any shell screen is opened, so a saved choice is the one the next boot
 * applies. The parser is deliberately strict: an out-of-range value is
 * ignored, never clamped into a state the user did not choose.
 *
 * /etc lives in the initramfs, so it does not survive power-off. The durable
 * copy is a small record in the tail of p77 (the only partition this project
 * writes): magic, version, length, CRC32, then the same key=value text. The
 * bootloader reads a p77 image by the sizes in its header, so a record parked
 * far past any real image is invisible to it; reflashing an image of the
 * usual ~24 MiB leaves the slot untouched. See PREFS_P77_SLOT_OFFSET.
 */

#ifndef UMOS_PREFS_H
#define UMOS_PREFS_H

#include <stddef.h>

#define PREFS_DEFAULT_PATH "/etc/qeaf.conf"

/* Durable record. 64 KiB is the reserved slot; a record is only ~100 bytes,
 * so a save writes that many bytes rather than the whole slot. */
#define PREFS_BLOCK_MAGIC   "QEAFPRF1"
#define PREFS_BLOCK_VERSION 1u
#define PREFS_BLOCK_HEADER  32u
#define PREFS_BLOCK_MAX     192u
#define PREFS_P77_PATH      "/dev/mmcblk0p77"
#define PREFS_P77_SIZE      (64u * 1024u * 1024u)
#define PREFS_P77_SLOT_SIZE (64u * 1024u)
#define PREFS_P77_SLOT_OFFSET (PREFS_P77_SIZE - PREFS_P77_SLOT_SIZE)

/* Test hook: point the store at a fixture file. NULL/empty keeps the default.
 * Resets the "already saved" cache so the next sync writes once. */
void prefs_set_path(const char *path);

/* Test / boot hook: enable the raw-block copy at `offset` bytes into `path`.
 * Pass NULL or an empty path to disable it. Reset the cache so the next sync
 * writes once. On the device init passes PREFS_P77_PATH and
 * PREFS_P77_SLOT_OFFSET; a gate passes a fixture file and a small offset. */
void prefs_set_block(const char *path, size_t offset);

/* "p77 slot", "/etc/qeaf.conf" or "none": which copy the last load/save used.
 * Never NULL. */
const char *prefs_source(void);

/* Reads the file. Values are written back through the pointers only when the
 * matching key parses and passes its range check. Returns 0 on a readable
 * file (even if some keys were ignored), -1 when it is missing or unreadable. */
int prefs_load(int *language, int *orientation, int *auto_rotate);

/* Writes all three values. Returns 0 on success, a negative sysio error
 * otherwise. Invalid values are refused before anything is written. */
int prefs_save(int language, int orientation, int auto_rotate);

/* Saves only when one of the three differs from what was last loaded/saved.
 * Returns 1 when a write happened, 0 when nothing changed, or the negative
 * error from prefs_save(). */
int prefs_sync(int language, int orientation, int auto_rotate);

#endif
