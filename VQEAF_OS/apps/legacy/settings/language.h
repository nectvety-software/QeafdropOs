/* SPDX-License-Identifier: MIT */
/* apps/settings/language.h - the Language sheet.
 *
 * Seven languages plus an automatic entry, laid out like a Material dialog:
 * a small accent overline, the title, then rows with a two-letter chip, the
 * language name, its native name and a check mark on the active one. Selecting
 * a row changes core/i18n immediately; BACK is handled by settings.c.
 */

#ifndef UMOS_LANGUAGE_H
#define UMOS_LANGUAGE_H

#include <stdint.h>

void language_init(void);
void language_open(void);
void language_close(void);
int  language_visible(void);
void language_draw(void);
/* 1 when the tap hit a row (and the language changed), 0 otherwise. */
int  language_event(uint32_t type, uint32_t d1, uint32_t d2);

/* Geometry shared by draw and hit test, so a measured tap lands on the row the
 * user saw. Returns 0 for an out-of-range index. */
int  language_row_rect(int index, int *x, int *y, int *w, int *h);
int  language_row_count(void);

#endif
