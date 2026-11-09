#!/usr/bin/env python3
"""Generate the two QeafdropOs home wallpapers.

The desktop shell fills the Redmi Note 7 Pro panel at its native aspect:
portrait 540x1170 (1080x2340 at 2x, no bars) and landscape 1170x540.
One 800x480 asset cannot fill both shapes without severe stretching, so
this script renders the same blue "silk" scene at each aspect.

Pure stdlib (zlib + struct): the build host has no Pillow.
"""

import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "custom", "assets", "ios")


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, w, h, pixel):
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            rows.extend(pixel(x, y))
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(rows), 6)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as handle:
        handle.write(png)
    return len(png)


def clamp(value, low, high):
    return low if value < low else high if value > high else value


def scene(w, h):
    """Return a function (x, y) -> (r, g, b) for a w x h blue silk scene."""
    # Normalised coordinates keep the composition identical in both aspects.
    def pixel(x, y):
        u = x / (w - 1)
        v = y / (h - 1)

        # Base gradient: deep navy at the top to a lit blue at the bottom.
        base_r = 9 + 20 * v + 10 * u
        base_g = 30 + 62 * v + 14 * u
        base_b = 64 + 92 * v + 26 * u

        r, g, b = base_r, base_g, base_b

        # Broad soft glows: the rounded highlights of the original wallpaper.
        glows = (
            (0.12, 0.88, 0.42, 46, 74, 96),
            (0.86, 0.16, 0.30, 34, 58, 80),
            (0.52, 0.46, 0.34, 18, 34, 52),
            (0.92, 0.78, 0.26, 26, 46, 66),
        )
        for cx, cy, radius, dr, dg, db in glows:
            dx = u - cx
            dy = (v - cy) * (h / w)
            d2 = (dx * dx + dy * dy) / (radius * radius)
            weight = math.exp(-d2 * 2.2)
            r += dr * weight
            g += dg * weight
            b += db * weight

        # Two smooth "silk" folds: a bright rim with a darker shadow beside it.
        folds = (
            (0.30, 2.4, 0.10, 1.05, 30, 52, 74),
            (0.62, 1.7, 0.22, 1.35, 22, 42, 62),
        )
        for offset, frequency, phase, width, dr, dg, db in folds:
            center = offset + 0.055 * math.sin(frequency * v + phase)
            d = (u - center) / width
            rim = math.exp(-(d * d) * 18.0)
            shade = math.exp(-((d - 0.16) * (d - 0.16)) * 8.0)
            r += dr * rim - 10 * shade
            g += dg * rim - 12 * shade
            b += db * rim - 8 * shade

        return (int(clamp(r, 0, 255)), int(clamp(g, 0, 255)),
                int(clamp(b, 0, 255)))

    return pixel


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, w, h in (("wallpaper-portrait.png", 540, 1170),
                       ("wallpaper-landscape.png", 1170, 540)):
        path = os.path.join(OUT, name)
        size = write_png(path, w, h, scene(w, h))
        print("wrote %s %dx%d (%d bytes)" % (name, w, h, size))


if __name__ == "__main__":
    main()
