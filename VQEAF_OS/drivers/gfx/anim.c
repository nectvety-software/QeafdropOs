/* SPDX-License-Identifier: MIT */
/* gui/gfx/anim.c - integer tween for shell transitions. */

#include "anim.h"

int anim_ease_out_cubic_q(int t_q)
{
    int d;

    if (t_q <= 0) return 0;
    if (t_q >= ANIM_Q) return ANIM_Q;
    d = ANIM_Q - t_q;
    return ANIM_Q - (d * d * d) / (ANIM_Q * ANIM_Q);
}

void anim_set(struct anim *a, int to_q, uint32_t dur_ms)
{
    if (a->to_q == to_q && a->started) return;

    /* Re-targeting mid-flight starts from wherever the value actually is, so
     * a gesture that reverses does not snap back to the old endpoint. */
    a->from_q = a->value_q;
    a->to_q = to_q;
    a->dur_ms = dur_ms;
    a->started = 0;
    a->t0_ms = 0;
    if (dur_ms == 0) a->value_q = to_q;
}

int anim_step(struct anim *a, uint64_t now_ms)
{
    uint64_t elapsed;
    int t_q, e_q;

    if (a->value_q == a->to_q) {
        a->started = 1;
        return 0;
    }
    if (!a->started) {
        a->t0_ms = now_ms;
        a->started = 1;
    }
    if (a->dur_ms == 0) {
        a->value_q = a->to_q;
        return 0;
    }

    elapsed = now_ms >= a->t0_ms ? now_ms - a->t0_ms : 0;
    t_q = elapsed >= a->dur_ms ? ANIM_Q
                               : (int)((elapsed * ANIM_Q) / a->dur_ms);
    e_q = anim_ease_out_cubic_q(t_q);
    a->value_q = a->from_q + ((a->to_q - a->from_q) * e_q) / ANIM_Q;
    if (t_q == ANIM_Q) a->value_q = a->to_q;

    return a->value_q != a->to_q;
}

void anim_finish(struct anim *a)
{
    a->value_q = a->to_q;
    a->from_q = a->to_q;
    a->started = 1;
    a->t0_ms = 0;
}
