"""A chained control session on the phone: every step is decided by what the
glass answered to the last one.

The scenario in `build/autotest.py` is a *test*: a fixed list of 24 shots, each
with its own gate, and its value is the pass line. This file is the other thing -
driving the phone the way a hand does, screen by screen, and reading back after
each move which window the shell says it is on, which overlay is up, what the
connector is doing, and a picture of it. The point is the chain: a step that
lands somewhere unexpected is reported as the dead end a person would be stuck
in, with the picture and the phone's own rect table to say so, and the session
continues from there instead of stopping at a red.

Steps are chained on purpose. The first pair is the sequence a person makes when
they pick the phone up: the lock goes up, the key darkens the glass, and ONE
swipe starting on the dark panel has to get them in - the defect that was being
argued about, run inside a session rather than as a probe.

Each step is one batch on the cable (defect #99: a host fetch costs about a
minute, longer than the shell's 60 s idle timer, so a gesture promised by an
earlier round trip is undone before the picture).

Run it with nothing else on the rescue volume.

    py -3.12 -u build/drive_session.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import autotest as at

OUTDIR = os.path.join(at.ROOT, "out", "session")
UNLOCK_SWIPE = "swipe 540 2050 540 1650"


def step(idx, name, cmds, expect, wake=False):
    """One batch: the gestures, then the picture and the three answers that
    describe the moment it was taken. Returns the record, or None if the phone
    never answered this batch (the session stops - without an answer there is
    nothing to chain on)."""
    try:
        cmds = at.expand(cmds)
    except at.MissingTarget as m:
        print("   skipped: the shell draws no %r rect on this boot" % m.key)
        return None
    if cmds is None:
        print("   skipped: no measured rect behind this step's target")
        return None
    print("\n== step %02d %s" % (idx, name))
    print("   plan: %s" % " | ".join(cmds))
    batch = at.shot_batch(cmds, wake=wake)
    ok, _ = at.send(batch)
    if not ok:
        print("   the channel refused this batch; nothing further can be chained")
        return None
    stem = "s%02d%s" % (idx, name.replace(" ", "_")[:14])
    text = at.read_back(stem, batch, tries=3, gap=8.0)
    if text is None:
        print("   no answer came back, so this step measured nothing")
        return None
    sections = dict(at.split_transcript(text))
    marker = at.parse_marker(sections.get("cat " + at.MARKER, "")) or {}
    glass = at.glass_state(sections)
    out = os.path.join(OUTDIR, stem)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "transcript.txt"), "w",
              encoding="utf-8", newline="\n") as f:
        f.write(text)
    dec = at.decode_map(sections.get("cat " + at.MAP_CANDIDATES[0], ""))
    png = ""
    if dec:
        cols, rows, scale, panel, rgb, total, origin = dec
        png = at.png_write(os.path.join(out, "map.png"), cols, rows, rgb)
        at.png_write(os.path.join(out, "map-8x.png"), cols, rows, rgb, 8)
        distinct, ink, top = at.stats(rgb)
        print("   map: %dx%d at 1/%d, %d colours, %.1f%% ink # #%02x%02x%02x"
              % (cols, rows, scale, distinct, ink * 100, top[0], top[1], top[2]))
        if distinct < 6 or ink <= 0.02:
            print("   note: a flat picture - the glass is showing a fill, not a "
                  "screen (distinct=%d, ink=%.3f)" % (distinct, ink))
    else:
        print("   note: no picture came back, so this step is marker-only")
    rec = {"idx": idx, "name": name, "marker": marker, "glass": glass,
           "png": png, "expect": expect, "ok": True}
    print("   marker: seq %s current=%r overlay %s gesture=%r recents %s nav %s"
          % (marker.get("seq"), marker.get("current"), marker.get("overlay"),
             marker.get("gesture"), marker.get("recents"), marker.get("nav")))
    print("   glass: %r" % glass)
    got = {"current": marker.get("current"), "overlay": marker.get("overlay"),
           "gesture": marker.get("gesture"), "glass": glass}
    misses = ["%s: want %r, phone says %r" % (k, v, got.get(k))
              for k, v in (expect or {}).items() if got.get(k) != v]
    for m in misses:
        print("   NOT AS PLANNED - %s" % m)
    rec["misses"] = misses
    rec["ok"] = not misses
    return rec


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    if not at.warm_volume():
        print("session: the rescue channel would not answer; nothing was driven")
        return 1
    if not at.start_rig():
        print("session: the rig would not start, so no step below can move the glass")
        return 1
    at.stamp_clock()

    recs = []
    # 00 - lock page up, then the power key darkens it: the state a phone is in
    # when it is picked up off a table. The picture here is of a dark panel and
    # says so, which is what makes step 01 an honest test.
    r = step(0, "khoa-va-tat-kinh", ["lock", "tsim key 150"],
             {"current": "Khoa", "glass": "Off"})
    recs.append(r)
    if r is None:
        return 1
    # 01 - the way in: ONE swipe that starts on the dark glass, no power key in
    # this batch, so nothing but the finger can light the panel.
    r = step(1, "mot-cuot-vuot-mo-khoa", [UNLOCK_SWIPE],
             {"current": "Home", "glass": "On"})
    recs.append(r)
    if r is None:
        return 1
    # 02/03 - the notification shade down from the top-left, and pushed back up.
    recs.append(step(2, "keo-shade", at.gesture_shade(), {}))
    recs.append(step(3, "dong-shade", ["swipe 540 700 540 4"], {"current": "Home"}))
    # 04 - quick settings from the top-right, then the Wi-Fi card the phone
    # itself measured: the tap is aimed at the rect the shell reported, not at a
    # coordinate this file guessed.
    recs.append(step(4, "quick-settings", at.gesture_cc(), {}))
    at.prime_rects(force=True)
    wifi = "tap @cc:wifi" if "cc:wifi" in at.RECTS else None
    if wifi:
        recs.append(step(5, "cham-the-wifi", [wifi], {}))
    else:
        print("\n== step 05 cham-the-wifi: SKIPPED - the shell reported no "
              "'cc:wifi' rect on this boot, so there is no measured card to tap")
        recs.append(None)
    recs.append(step(6, "dong-cc-ve-home", ["swipe 540 700 540 4"] + at.gesture_back(),
                     {"current": "Home"}))
    # 07/08 - the drawer, then an app chosen from the rects the drawer itself
    # published: this is the chain, not a script with remembered coordinates.
    recs.append(step(7, "mo-drawer", at.gesture_drawer(), {}))
    at.prime_rects(force=True)
    tiles = sorted(k for k in at.RECTS if k.startswith("tile"))
    print("   tiles the phone measures right now: %s" % (", ".join(tiles) or "none"))
    if tiles:
        recs.append(step(8, "mo-app-tu-drawer", ["tap @%s" % tiles[0]], {}))
        app_page = (recs[-1] or {}).get("marker", {}).get("current")
        recs.append(step(9, "lua-app-quay-lai", at.gesture_back(), {}))
    else:
        print("\n== step 08: SKIPPED - no tile rect on the glass, so the session "
              "has nothing it can honestly aim at")
        recs.append(None)
        app_page = None
    # 09/10 - page turns at Home, photographed on the way out and back: the pair
    # has to agree, or the grid does not come home.
    recs.append(step(10, "vuot-trang-trai", at.page_turn(-1), {}))
    recs.append(step(11, "vuot-trang-phai", at.page_turn(1), {}))
    # 12 - recents: up from the bottom and rest, with one app in the stack.
    recs.append(step(12, "recents", at.gesture_recents_glide(), {}))

    print("\n== session table")
    bad = 0
    for r in recs:
        if r is None:
            print("   - (no answer / skipped)")
            bad += 1
            continue
        m = r["marker"]
        flag = "ok " if r["ok"] else "RED"
        print("   %s %02d %-24s current=%-18r overlay=%-2s gesture=%-10r glass=%r  %s"
              % (flag, r["idx"], r["name"], m.get("current"), m.get("overlay"),
                 m.get("gesture"), r["glass"], os.path.relpath(r["png"], at.ROOT)
                 if r["png"] else "-"))
        bad += len(r["misses"])
    print("\nsession: %d steps, %d step(s) that did not land where planned"
          % (len(recs), bad))
    print("session: pictures under %s" % OUTDIR)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
