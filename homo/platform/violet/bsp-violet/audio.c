/* SPDX-License-Identifier: MIT */
/* bsp/violet/audio.c - Audio codec stub */

#include <stdint.h>

int violet_audio_init(void) {
    return 0;
}

int violet_audio_beep(uint16_t freq_hz, uint32_t ms) {
    (void)freq_hz; (void)ms;
    return 0;
}
