/* SPDX-License-Identifier: MIT */
/* gui/gfx/anim.h - integer tween used by every shell transition.
 *
 * No floats and no clock of its own: the caller hands in the timestamp, which
 * is what lets the host gate step a transition frame by frame and measure the
 * glass instead of trusting a wall clock. Values are q8 fixed point.
 */

#ifndef UMOS_ANIM_H
#define UMOS_ANIM_H

#include <stdint.h>

#define ANIM_Q 256 /* unity in this file's fixed point */

struct anim {
    uint64_t t0_ms;   /* 0 until the first frame sees a changed target */
    uint32_t dur_ms;
    int from_q;
    int to_q;
    int value_q;
    int started;
};

/* Arms the tween. The start time is taken on the first compositor_frame()
 * after this, so opening a window needs no clock. */
void anim_set(struct anim *a, int to_q, uint32_t dur_ms);

/* Advances to now_ms and returns 1 while the value is still moving. */
int anim_step(struct anim *a, uint64_t now_ms);

/* Snaps to the target without a ramp: for the first screen the shell shows,
 * which the user never saw in its previous state. */
void anim_finish(struct anim *a);

/* Ease-out cubic in q8: fast at the start, asymptotic at the end, which is
 * the shape that reads as "smooth" for a zoom. */
int anim_ease_out_cubic_q(int t_q);

#endif
