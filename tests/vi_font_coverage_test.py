#!/usr/bin/env python3
"""Vietnamese glyph gate: the UI face must actually carry every catalog char.

The catalog now stores Vietnamese with diacritics. tiny_ttf returns a missing
glyph as "not found", so the label would fall through to PingFang (which has no
Vietnamese at all) and then to the shaper's box - a screen a host render gate
still draws happily because it only checks what LVGL reports. This gate reads
the real cmap of the shipped face and fails when a catalog character is absent.

In-run controls, so a broken parse cannot pass as coverage:
  * every VI string must be non-empty (a zero sample proves nothing),
  * a character the face is known not to have (CJK) must be reported absent,
  * that same character must be present in the CJK fallback face, which is what
    makes the fallback chain load-bearing rather than decorative.

    python tests/vi_font_coverage_test.py            # gate: exit 1 on a finding
"""

import importlib.util
import os
import struct
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOMO = os.path.join(ROOT, "homo")
LATIN_TTF = os.path.join(HOMO, "custom", "assets", "ios",
                         "LiberationSans-Regular.ttf")
CJK_TTF = os.path.join(HOMO, "custom", "assets", "ios", "PingFang.ttf")
FONTS_C = os.path.join(HOMO, "framework", "ui", "fonts.c")
CATALOGS = [os.path.join(HOMO, "framework", "ui", "i18n.c"),
            os.path.join(HOMO, "framework", "ui", "i18n_apps.inc")]


def load_gate():
    path = os.path.join(ROOT, "tests", "i18n_catalog_test.py")
    spec = importlib.util.spec_from_file_location("i18n_catalog_test", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def u16(data, off):
    return struct.unpack_from(">H", data, off)[0]


def u32(data, off):
    return struct.unpack_from(">I", data, off)[0]


def cmap_codepoints(path):
    """Codepoints the face can draw, from its real cmap subtables."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 12:
        raise ValueError("%s is too short to be a TrueType file" % path)
    num_tables = u16(data, 4)
    cmap = None
    for i in range(num_tables):
        rec = 12 + i * 16
        if rec + 16 > len(data):
            break
        if data[rec:rec + 4] == b"cmap":
            cmap = u32(data, rec + 8)
            break
    if cmap is None:
        raise ValueError("%s has no cmap table" % path)

    points, subtables = set(), 0
    for i in range(u16(data, cmap + 2)):
        rec = cmap + 4 + i * 8
        plat, enc = u16(data, rec), u16(data, rec + 2)
        sub = cmap + u32(data, rec + 4)
        fmt = u16(data, sub)
        if fmt == 4 and (plat == 0 or (plat == 3 and enc in (1, 0))):
            seg_x2 = u16(data, sub + 6)
            ends = sub + 14
            starts = ends + seg_x2 + 2
            deltas = starts + seg_x2
            ranges = deltas + seg_x2
            for seg in range(seg_x2 // 2):
                end = u16(data, ends + seg * 2)
                start = u16(data, starts + seg * 2)
                delta = u16(data, deltas + seg * 2)
                ro = u16(data, ranges + seg * 2)
                if start > end:
                    continue
                for cp in range(start, end + 1):
                    if cp == 0xFFFF:
                        continue
                    if ro == 0:
                        glyph = (cp + delta) & 0xFFFF
                    else:
                        gaddr = ranges + seg * 2 + ro + (cp - start) * 2
                        if gaddr + 2 > len(data):
                            continue
                        glyph = u16(data, gaddr)
                        if glyph:
                            glyph = (glyph + delta) & 0xFFFF
                    if glyph:
                        points.add(cp)
            subtables += 1
        elif fmt == 12 and plat in (0, 3):
            groups = u32(data, sub + 12)
            base = sub + 16
            for g in range(groups):
                off = base + g * 12
                if off + 12 > len(data):
                    break
                lo, hi = u32(data, off), u32(data, off + 4)
                if lo > hi or hi - lo > 0x20000:
                    continue
                points.update(range(lo, hi + 1))
            subtables += 1
    if subtables == 0:
        raise ValueError("%s has no usable cmap subtable" % path)
    return points


def vi_strings(gate):
    text = (gate.strip_comments(gate.read(CATALOGS[0])) + "\n" +
            gate.strip_comments(gate.read(CATALOGS[1])))
    return [triple[2] for triple in gate.parse_triples(text)]


def main():
    gate = load_gate()
    problems = []
    strings = vi_strings(gate)
    latin = cmap_codepoints(LATIN_TTF)
    cjk = cmap_codepoints(CJK_TTF)
    print("faces: latin=%d codepoints, cjk-fallback=%d codepoints"
          % (len(latin), len(cjk)))
    print("catalog: %d Vietnamese string(s)" % len(strings))

    if not strings:
        problems.append("no Vietnamese string was parsed out of the catalogs")
    if len(latin) < 500:
        problems.append("latin cmap parse returned %d codepoints - parser is "
                        "not observing the face" % len(latin))

    # Negative control: the primary face must genuinely lack CJK, otherwise
    # "every character is covered" would be true for the wrong reason.
    probe = 0x4E00  # 一, present in the zh column of the catalog
    if probe in latin:
        problems.append("negative control: U+4E00 is in the latin cmap, so this "
                        "gate cannot tell coverage from a permissive parse")
    if probe not in cjk:
        problems.append("fallback control: U+4E00 is missing from the CJK face "
                        "too - the fallback chain would draw a box")

    missing = {}
    for text in strings:
        for ch in text:
            if ord(ch) < 0x80:
                continue
            if ord(ch) not in latin:
                missing.setdefault(ch, text)
    for ch in sorted(missing):
        problems.append("U+%04X %r is not in the UI face (in %r)"
                        % (ord(ch), ch, missing[ch][:40]))

    for problem in problems:
        print("FONT: %s" % problem)

    fonts_src = gate.read(FONTS_C)
    if "A:assets/ios/LiberationSans-Regular.ttf" not in fonts_src:
        problems.append("fonts.c no longer loads the Latin face first")
    if "->fallback" not in fonts_src:
        problems.append("fonts.c no longer chains the CJK fallback")

    print("RESULT: %s" % ("FAIL" if problems else "PASS"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
