/* SPDX-License-Identifier: MIT */
/* gui/gfx/font_5x7.h */

#ifndef UMOS_FONT_5X7_H
#define UMOS_FONT_5X7_H

#include "font.h"

extern struct font font_5x7;
void font_5x7_init(void);
const struct font_glyph *font_5x7_ext(uint32_t cp);

#endif
