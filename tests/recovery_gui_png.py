#!/usr/bin/env python3
"""Convert the host gate's raw RGB565 framebuffer into viewable PNGs.

Pure stdlib (zlib + struct): this repo has no Pillow on the host.
Usage: python tests/recovery_gui_png.py <raw> <out-prefix> [w] [h]
Writes <prefix>.png (full), <prefix>_half.png (1:1 dp view) and
<prefix>_top.png (header + first action rows, 1:1 crop).
"""

import array
import struct
import sys
import zlib


def png_chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, w, h, get_pixel):
    rows = bytearray()
    for y in range(h):
        rows.append(0)  # filter type: None
        line = bytearray()
        for x in range(w):
            line.extend(get_pixel(x, y))
        rows.extend(line)
    out = (b"\x89PNG\r\n\x1a\n" +
           png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           png_chunk(b"IDAT", zlib.compress(bytes(rows), 6)) +
           png_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


def rgb888(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (((r << 3) | (r >> 2)),
            ((g << 2) | (g >> 4)),
            ((b << 3) | (b >> 2)))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    raw_path, prefix = sys.argv[1], sys.argv[2]
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 1080
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 2340

    data = open(raw_path, "rb").read()
    pix = array.array("H")
    pix.frombytes(data)
    if sys.byteorder != "little":
        pix.byteswap()
    if len(pix) != w * h:
        print("size mismatch: %d samples, expected %d" % (len(pix), w * h))
        return 2

    cache = {}

    def colour(x, y):
        v = pix[y * w + x]
        c = cache.get(v)
        if c is None:
            c = rgb888(v)
            cache[v] = c
        return c

    n = write_png(prefix + ".png", w, h, colour)
    print("%s.png  %dx%d  %d bytes  %d colours" %
          (prefix, w, h, n, len(cache)))

    hw, hh = w // 2, h // 2
    n = write_png(prefix + "_half.png", hw, hh,
                  lambda x, y: colour(x * 2, y * 2))
    print("%s_half.png  %dx%d  %d bytes" % (prefix, hw, hh, n))

    top_h = min(900, h)
    n = write_png(prefix + "_top.png", w, top_h, colour)
    print("%s_top.png  %dx%d  %d bytes" % (prefix, w, top_h, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
