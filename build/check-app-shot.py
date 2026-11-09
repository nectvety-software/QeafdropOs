#!/usr/bin/env python3
"""build/check-app-shot.py - the rows build/app-shot.sh is asked to prove.

A picture of the simulator's glass says an app page was painted. It does not say a
program ran. This file is where the difference gets decided, and it decides it from
three sources that have to agree:

  the transcript   what production init logged when the press landed, what the
                   watch reported as the machine's own process id, and how many
                   frame requests actually crossed the door
  the run directory  the folder init's own ensure_dir() made, which has to hold the
                   payload extract_payload() wrote *and* the file the program wrote
                   beside it - the shipped extraction and the running program, in
                   one place
  the pictures     two frames of the program's own palette, which have to differ in
                   the two things only a frame count changes, and an OS page on
                   either side of them that does not have that palette

Nothing here re-derives what the app should look like from apps/demo/demo.c's
source text: the colours and the geometry are the numbers that file paints with, so
a change to the program moves this gate and the run has to be looked at again.
"""
import os
import sys

from PIL import Image

FIELD = (0x24, 0x0A, 0x3C)
BAND = (0x35, 0xE2, 0xF0)
TICK_LIT = (0xF7, 0xA8, 0x3C)
TICK_OFF = (0x5A, 0x2A, 0x74)
FRAME_W = 540
FRAME_H = 1170
RULE_ROWS = ((FRAME_H - 70, FRAME_H - 62), (FRAME_H - 26, FRAME_H - 18))
TICK_Y = (FRAME_H - 54, FRAME_H - 34)
TICKS = 24


def rgb565_roundtrip(c):
    """The frame's bytes go through the panel's RGB565 surface and come back, so
    the colour a picture has to match is the one that survives that trip - the same
    two steps a phone's glass takes."""
    r, g, b = c
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    r5, g6, b5 = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return ((r5 * 255 + 15) // 31, (g6 * 255 + 3) // 63, (b5 * 255 + 15) // 31)


FIELD_PX = rgb565_roundtrip(FIELD)
BAND_PX = rgb565_roundtrip(BAND)
LIT_PX = rgb565_roundtrip(TICK_LIT)
OFF_PX = rgb565_roundtrip(TICK_OFF)


class Gate:
    def __init__(self):
        self.rows = []
        self.failed = 0

    def row(self, name, ok, detail):
        self.rows.append((name, ok, detail))
        if not ok:
            self.failed += 1
        print("%-4s %-46s %s" % ("PASS" if ok else "FAIL", name, detail))

    def finish(self):
        print("check-app-shot: %d rows, %d fail" % (len(self.rows), self.failed))
        return 1 if self.failed else 0


def load(path):
    if not os.path.exists(path):
        return None
    img = Image.open(path).convert("RGB")
    return img


def field_fraction(img):
    px = img.load()
    w, h = img.size
    hit = 0
    for y in range(h):
        for x in range(w):
            if px[x, y] == FIELD_PX:
                hit += 1
    return hit / float(w * h)


def band_rows(img):
    """The y's the app's moving band covers, as the picture shows them. The band is
    the program's frame counter made visible, so its first row is a number a still
    picture can be asked for."""
    px = img.load()
    w, h = img.size
    rows = []
    for y in range(h):
        hit = 0
        for x in range(0, w, 4):
            if px[x, y] == BAND_PX:
                hit += 1
        if hit > (w // 4) * 0.9:
            rows.append(y)
    return rows


def lit_tick(img):
    """Which of the twenty-four cells along the lower rule is lit - the frame's own
    index, counted from the geometry apps/demo/demo.c fills."""
    px = img.load()
    step = (FRAME_W - 16) // TICKS
    y0, y1 = TICK_Y
    ymid = (y0 + y1) // 2
    lit = []
    for i in range(TICKS):
        x0 = 8 + i * step
        if x0 + 2 >= FRAME_W:
            continue
        c = px[x0 + 2, ymid]
        if c == LIT_PX:
            lit.append(i)
    return lit


def rules_present(img):
    px = img.load()
    w = img.size[0]
    for (a, b) in RULE_ROWS:
        hit = 0
        for x in range(0, w, 4):
            if px[x, a + 1][0] > 200 and px[x, a + 1][2] > 200:
                hit += 1
        if hit < (w // 4) * 0.9:
            return False
    return True


def main(argv):
    if len(argv) < 8:
        print("usage: check-app-shot.py TRANSCRIPT PNG... (5) RUNDIRECTORY")
        return 2
    transcript, pngs, rundir = argv[1], argv[2:7], argv[7]
    g = Gate()

    if not os.path.exists(transcript):
        g.row("transcript exists", False, transcript)
        return g.finish()
    with open(transcript, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    lines = text.splitlines()
    g.row("transcript exists", True, "%d lines, %d bytes" % (len(lines), len(text)))

    # ---- what production init said
    opened = [l for l in lines if "đã mở demo pid" in l]
    g.row("init unpacked and forked the package", bool(opened),
          opened[0].strip() if opened else "no 'đã mở demo pid' line")

    page = [l for l in lines if "Ứng dụng thử" in l]
    g.row("the page after the press names the program", bool(page),
          page[0].strip() if page else "no line names the package's display name")

    started = [l for l in lines if "app 'demo' chạy thật" in l]
    g.row("the machine ran a real process for it", bool(started),
          started[0].strip() if started else "no 'chạy thật' line")

    frames = [l for l in lines if l.startswith("< app: frame ")]
    g.row("frames crossed the door", len(frames) >= 4,
          "%d 'frame' requests from the program" % len(frames))

    # The verb's own answer, not a guess: `frame` replies "frame handed to the
    # panel" when panel_present() took the bytes, and anything else is a refusal
    # that left a status number in the transcript.
    handed = [l for l in lines if "frame handed to the panel" in l]
    g.row("the panel took them", len(handed) >= len(frames),
          "%d requests in, %d accepted%s" % (
              len(frames), len(handed),
              "" if len(handed) >= len(frames) else
              " (statuses: %s)" % ", ".join(l.strip() for l in lines
                                           if l.strip().startswith("[status"))))

    gave_up = [l for l in lines if "tự thoát" in l]
    g.row("the program never gave up", not gave_up,
          gave_up[0].strip() if gave_up else "it was still painting at the stop")

    stopped = [l for l in lines if "init không còn giữ kính" in l]
    g.row("the press that gives the glass back ended it", bool(stopped),
          stopped[0].strip() if stopped else "the process was still alive at exit")

    # ---- the directory the shipped code made
    appbin = os.path.join(rundir, "app.bin")
    frame = os.path.join(rundir, "frame.rgb")
    want = os.path.getsize("out/apps/demo.elf") if os.path.exists(
        "out/apps/demo.elf") else 0
    ok = os.path.exists(appbin) and os.path.getsize(appbin) == want
    g.row("the payload the shipped reader unpacked", ok,
          "%s is %s bytes, demo.elf is %d" % (
              appbin, os.path.getsize(appbin) if os.path.exists(appbin) else "-",
              want))
    ok = os.path.exists(frame) and os.path.getsize(frame) == FRAME_W * FRAME_H * 3
    g.row("the program wrote beside it", ok,
          "%s is %s bytes, one frame needs %d" % (
              frame, os.path.getsize(frame) if os.path.exists(frame) else "-",
              FRAME_W * FRAME_H * 3))

    # ---- the pictures
    imgs = {}
    for p in pngs:
        im = load(p)
        imgs[p] = im
        g.row("picture exists", im is not None,
              "%s%s" % (p, "" if im is None else " %dx%d" % im.size))
    if any(v is None for v in imgs.values()):
        return g.finish()

    drawer, band_row, first, later, home = [imgs[p] for p in pngs]
    for p in (drawer, band_row, first, later, home):
        if p.size != (FRAME_W, FRAME_H):
            g.row("picture is the panel at 1/2", False,
                  "%dx%d" % p.size)
    names = dict(zip(pngs, ("01-drawer", "02-drawer-demo", "03-app-first",
                            "04-app-later", "05-launcher-again")))
    fr = {p: field_fraction(imgs[p]) for p in pngs}
    for p in pngs:
        g.row("%s is app or OS, not a mix" % names[p],
              fr[p] > 0.5 if p in (first, later) else fr[p] < 0.1,
              "%.1f%% of the glass is the program's field colour" % (fr[p] * 100))

    if first.size == later.size:
        b1, b2 = band_rows(first), band_rows(later)
        l1, l2 = lit_tick(first), lit_tick(later)
        g.row("the band moved between the two frames",
              bool(b1) and bool(b2) and b1[0] != b2[0],
              "rows %s then %s" % (b1[0] if b1 else "-", b2[0] if b2 else "-"))
        g.row("the frame counter changed",
              len(l1) == 1 and len(l2) == 1 and l1 != l2,
              "lit cell %s then %s of 24" % (l1, l2))
        g.row("both frames are the program's own layout",
              rules_present(first) and rules_present(later),
              "the two white rules the program paints")
        diff = sum(1 for y in range(0, first.size[1], 7)
                   for x in range(0, first.size[0], 7)
                   if first.load()[x, y] != later.load()[x, y])
        total = len(range(0, first.size[1], 7)) * len(range(0, first.size[0], 7))
        g.row("the two pictures differ", diff > total * 0.01,
              "%d of %d sampled pixels changed" % (diff, total))

    g.row("the drawer before the press is a page of the OS",
          drawer.load()[10, 10] != FIELD_PX and band_rows(drawer) == [],
          "and the band is on the program's row in the next one: %s" %
          ("differs" if drawer.tobytes() != band_row.tobytes() else "IDENTICAL"))
    g.row("the glass came back to the OS",
          home.load()[10, 10] != FIELD_PX and fr[home] < 0.1,
          "%.1f%% field colour after the stop press" % (fr[home] * 100))
    return g.finish()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
