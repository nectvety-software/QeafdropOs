#!/usr/bin/env python3
"""Build a p77 image whose on-device GUI no longer says "Android".

The current image, ``out/boot-qeaf-violet-m48.img``, carries the brand word in
five cpio entries -- two of them paint or print user-visible text:

  ./bin/umos      (3) OS GUI: "Tra may ve Android ...", "Khoi dong vao Android",
                      "boot command cleared, restarting into Android"
  ./sbin/qeafinit (0) the storage init no longer carries the word at all

The patch replaces the 7-byte word "Android" with the 7-byte "System " (matching
the source GUI's "system slot" wording). Equal-length replacement means no cpio
offsets, header layout or ELF relocations move. The ramdisk is re-gzipped and
the v1 header gets the new ramdisk_size. Kernel, cmdline, addresses and page
size are copied byte-for-byte.

Without --all only the two GUI binaries above are patched. With --all every
cpio entry is patched too -- the shipped m48 image used --all, which removed 9
occurrences across ./bin/umos (3), ./bin/qeaf-wifi (3), ./bin/qeaf-report (1),
./etc/qeaf/fstab (1) and ./etc/qeaf/ueventd.cfg (1); the last four are comments.

Deliberately NOT changed:
  * the protocol token ``ANDROID!`` (boot header magic in flash error messages).

The result MUST pass scripts/validate_bootimg.py before it is written.
"""

import argparse
import gzip
import hashlib
import io
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validate_bootimg import P77_SIZE, validate_bytes, validate_file  # noqa: E402

DEFAULT_IMAGE = "out/boot-qeaf-violet-m48.img"
DEFAULT_OUT = "out/boot-qeaf-violet-m48-noandroid.img"

# Binaries that paint or print user-visible UI text. ./bin/umos is the OS GUI;
# ./sbin/qeafinit drives the rescue framebuffer and prints the HELP screen.
GUI_BINARIES = {b"./bin/umos", b"./sbin/qeafinit"}
OLD = b"Android"
NEW = b"System "
MAGIC_TOKEN = b"ANDROID!"
assert len(OLD) == len(NEW), "replacement must not shift any offset"


def cpio_ranges(raw):
    """Yield (name, body_start, body_end) for each newc cpio entry."""
    off = 0
    while off + 110 <= len(raw):
        if raw[off:off + 6] not in (b"070701", b"070702"):
            break
        try:
            fields = [int(raw[off + 6 + i * 8:off + 14 + i * 8], 16)
                      for i in range(13)]
        except ValueError:
            break
        namesize, filesize = fields[11], fields[6]
        name = raw[off + 110:off + 110 + namesize - 1]
        start = off + 110 + namesize
        start += (4 - (start % 4)) % 4
        end = start + filesize
        yield name, start, end
        off = end + ((4 - (end % 4)) % 4)
        if name == b"TRAILER!!!":
            break


def patch_cpio(raw, all_entries=False):
    buf = bytearray(raw)
    changed_files = []
    total = 0
    for name, start, end in cpio_ranges(raw):
        if not all_entries and name not in GUI_BINARIES:
            continue
        body = bytes(buf[start:end])
        n = body.count(OLD)
        if n:
            buf[start:end] = body.replace(OLD, NEW)
            total += n
            changed_files.append((name.decode("ascii", "replace"), n))
    return bytes(buf), total, changed_files


def count_in_gui(raw):
    n = 0
    for name, start, end in cpio_ranges(raw):
        if name in GUI_BINARIES:
            n += raw[start:end].count(OLD)
    return n


def brand_in_gui(raw):
    """Case-sensitive 'Android' (capital A) in UI binaries, ignoring the
    ANDROID! token.

    The brand label is always capitalised — ``Android``. A lowercase
    ``android`` inside an ELF is a Zig/GCC target-triple platform identifier
    (``android``, ``eabi``, ``musl``) baked into ``.rodata`` by the compiler,
    not a user-visible string. The literal ``ANDROID!`` is a boot-header
    magic string quoted in flash error messages, not a brand label. Changing
    either of the latter two would corrupt the rescue protocol or patch a
    string the user never sees.
    """
    n = 0
    for name, start, end in cpio_ranges(raw):
        if name not in GUI_BINARIES:
            continue
        body = raw[start:end]
        for m in re.finditer(b"Android", body):
            if body[m.start():m.start() + len(MAGIC_TOKEN)] == MAGIC_TOKEN:
                continue
            n += 1
    return n


def main(argv=None):
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--image", default=DEFAULT_IMAGE)
    p.add_argument("--output", default=DEFAULT_OUT)
    p.add_argument("--all", action="store_true",
                   help="also replace Android in non-GUI files of the ramdisk")
    args = p.parse_args(argv)

    r = validate_file(args.image)
    if not r.ok:
        print("REFUSED: source image is not a valid p77 image", file=sys.stderr)
        for e in r.errors:
            print("  ERROR: %s" % e, file=sys.stderr)
        return 2
    print("source : %s (%d bytes, sha256 %s)"
          % (args.image, r.facts["size"], r.facts["sha256"][:16]))

    data = open(args.image, "rb").read()
    page = r.facts["page_size"]
    ks = r.facts["kernel_size"]
    ramdisk_off = r.facts["ramdisk_offset"]
    rs = r.facts["ramdisk_size"]

    plain = gzip.decompress(data[ramdisk_off:ramdisk_off + rs])
    before = count_in_gui(plain)
    brand_before = brand_in_gui(plain)
    magic_before = plain.count(MAGIC_TOKEN)
    patched, total, files = patch_cpio(plain, args.all)
    if total == 0:
        print("REFUSED: no 'Android' occurrences found - nothing to do",
              file=sys.stderr)
        return 2
    after = count_in_gui(patched)
    brand_after = brand_in_gui(patched)
    magic_after = patched.count(MAGIC_TOKEN)
    if after != 0 or brand_after != 0:
        print("REFUSED: %d exact / %d case-insensitive GUI occurrences survived "
              "the patch" % (after, brand_after), file=sys.stderr)
        return 2
    if magic_before != magic_after:
        print("REFUSED: the ANDROID! boot-header token was altered "
              "(%d -> %d)" % (magic_before, magic_after), file=sys.stderr)
        return 2
    if len(patched) != len(plain):
        print("REFUSED: ramdisk length changed - cpio offsets would move",
              file=sys.stderr)
        return 2
    print("patched: %d occurrence(s) in %s"
          % (total, ", ".join("%s(%d)" % f for f in files)))
    print("  UI binaries: %d -> %d 'Android' strings "
          "(case-insensitive %d -> %d)"
          % (before, after, brand_before, brand_after))
    print("  ANDROID! boot-magic tokens preserved: %d" % magic_after)
    print("  ramdisk plain length unchanged: %d bytes" % len(patched))

    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=6, mtime=0) as gz:
        gz.write(patched)
    new_rd = buf.getvalue()

    header = bytearray(data[:page])
    struct.pack_into("<I", header, 0x10, len(new_rd))
    kernel_blob = data[page:ramdisk_off]   # kernel + its page padding, verbatim
    tail_pad = (-len(new_rd)) % page
    out = bytes(header) + kernel_blob + new_rd + b"\x00" * tail_pad

    if len(out) > P77_SIZE:
        print("REFUSED: rebuilt image is %d bytes, larger than p77"
              % len(out), file=sys.stderr)
        return 2

    check = validate_bytes(out, args.output)
    if not check.ok:
        print("REFUSED: rebuilt image failed validation", file=sys.stderr)
        for e in check.errors:
            print("  ERROR: %s" % e, file=sys.stderr)
        return 2

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(out)

    print("output : %s (%d bytes, sha256 %s)"
          % (args.output, len(out), hashlib.sha256(out).hexdigest()))
    print("  kernel %d bytes UNCHANGED, ramdisk %d -> %d bytes"
          % (ks, rs, len(new_rd)))
    print("  [PASS] page=%d kernel_addr=0x%08X ramdisk_addr=0x%08X"
          % (check.facts["page_size"], check.facts["kernel_addr"],
             check.facts["ramdisk_addr"]))
    print("\nFlash ONLY to p77/recovery. The ANDROID! protocol token and 6 "
          "comment-only\noccurrences in scripts/config are intentionally left "
          "unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
