/* SPDX-License-Identifier: MIT */
/* apps/settings/features.h - "Features" page: a live status matrix.
 *
 * Every badge on this page is computed from a real getter. A feature that
 * needs hardware the board does not have says "no HW"; the AI row says
 * "host only" because this image carries no model. Nothing here pretends.
 */

#ifndef UMOS_FEATURES_H
#define UMOS_FEATURES_H

#include <stdint.h>

void features_open(void);
void features_close(void);
int  features_visible(void);
void features_draw(void);
int  features_event(uint32_t type, uint32_t d1, uint32_t d2);
void features_init(void);

#endif
