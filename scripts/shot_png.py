#!/usr/bin/env python3
"""Decode a rescue capture into PNGs so a person can look at the glass.

The rescue console answers a batch by writing one RESPONSE.TXT; the picture in
it is the `MAP cols rows scale pw ph ox oy` block that `snap` leaves in
/cache/umos/map.hex. Each sample is one byte of 3-3-2 colour, so the image is
small by contract (72x156 for the whole 1080x2340 panel at 1/15) and the only
honest way to read it is to blow it up without inventing pixels.

Usage:
    python scripts/shot_png.py out/t/menu-shot-1105/RESPONSE.TXT --prefix menu
"""

import argparse
import re
import os
import struct
import sys
import zlib


def section_bodies(text):
    """[(command, body)] split on the '### <line>' headers the board writes."""
    out = []
    parts = re.split(r"^### (.*)$", text, flags=re.M)
    for i in range(1, len(parts), 2):
        out.append((parts[i].strip(), parts[i + 1].lstrip("\n")))
    return out


def decode_map(text):
    m = re.search(r"^MAP (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)\n", text, re.M)
    if not m:
        return None
    cols, rows, scale, pw, ph, ox, oy = (int(m.group(i)) for i in range(1, 8))
    body = text[m.end():]
    want = cols * 2 + 1
    rgb = bytearray()
    total = 0
    for r in range(rows):
        line = body[r * want:(r + 1) * want]
        if r == rows - 1 and len(line) == want - 1:
            line += "\n"
        if len(line) < want or not re.fullmatch(r"[0-9a-f]+", line[:-1] or ""):
            return None
        for i in range(0, cols * 2, 2):
            b = int(line[i:i + 2], 16)
            rgb.append(((b >> 5) & 7) * 255 // 7)
            rgb.append(((b >> 2) & 7) * 255 // 7)
            rgb.append((b & 3) * 255 // 3)
            total = (total + b) & 0xFFFFFFFF
    return cols, rows, scale, (pw, ph), bytes(rgb), total, (ox, oy)


def png_write(path, w, h, rgb, scale_up=1):
    if scale_up > 1:
        raw = b""
        for y in range(h):
            line = bytearray()
            for x in range(w):
                line += rgb[y * w * 3 + x * 3:y * w * 3 + x * 3 + 3] * scale_up
            raw += (b"\x00" + bytes(line)) * scale_up
        w, h = w * scale_up, h * scale_up
    else:
        raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    data = zlib.compress(raw)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", data))
        f.write(chunk(b"IEND", b""))
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("response")
    ap.add_argument("--prefix", default="shot")
    ap.add_argument("--scale-up", type=int, default=8)
    args = ap.parse_args()

    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    text = open(args.response, "r", encoding="utf-8", errors="replace").read()

    marker = ""
    picture = None
    for cmd, body in section_bodies(text):
        if cmd.startswith("cat ") and "qeaf-tsim-map" in cmd:
            marker += body.strip() + "\n"
        dec = decode_map(body)
        if dec:
            picture = dec

    if not picture:
        print("NO PICTURE: no MAP block in %s" % args.response)
        return 1

    cols, rows, scale, panel, rgb, total, origin = picture
    base = os.path.dirname(args.response)
    small = os.path.join(base, "%s-%dx%d.png" % (args.prefix, cols, rows))
    big = os.path.join(base, "%s-%dx%d-x%d.png" % (args.prefix, cols, rows,
                                                   args.scale_up))
    png_write(small, cols, rows, rgb)
    png_write(big, cols, rows, rgb, args.scale_up)

    colours = len({rgb[i:i + 3] for i in range(0, len(rgb), 3)})
    ink = sum(1 for i in range(0, len(rgb), 3)
              if rgb[i] + rgb[i + 1] + rgb[i + 2] > 24)
    print("marker: %s" % (marker.strip() or "(none)"))
    print("picture: %dx%d scale %d panel %dx%d origin %s sum %d" %
          (cols, rows, scale, panel[0], panel[1], origin, total))
    print("distinct colours %d, lit samples %d of %d (%.1f%%)" %
          (colours, ink, cols * rows, 100.0 * ink / (cols * rows)))
    print("wrote %s" % small)
    print("wrote %s" % big)
    return 0


if __name__ == "__main__":
    sys.exit(main())
