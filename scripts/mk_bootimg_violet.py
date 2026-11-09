#!/usr/bin/env python3
"""Repack a violet boot image by patching the stock header in place.

Why not mkbootimg: violet's header is not the AOSP field order. Measured on
out/reference/stock-violet/boot.img:
    0x08 kernel_size      0x0c kernel_addr   0x10 ramdisk_size
    0x14 ramdisk_addr     0x18 second_size   0x1c second_addr
    0x20 tags_addr        0x24 page_size     0x28 header_version
    0x2c os_version       0x30 name[16]      0x40 cmdline[512]

kernel_addr lives at 0x0c, where AOSP keeps ramdisk_size, and cmdline starts at
0x40, where AOSP keeps the second half of name[]. The AVB footer settles the
argument: original_image_size is exactly one page plus the padded kernel, so
this particular file really has no ramdisk and 0x0c cannot be a size in it.

The stock file's kernel payload matches the p76 dump byte for byte from 4096 to
17510399 - the compared difference begins exactly at 17510400, the first byte
after the padded kernel, i.e. the ramdisk - which is why every address this
script copies from it (kernel_addr 0x8000, tags_addr 0x100, page_size 4096) is
the live value too.

Copying the stock page and rewriting only the fields we change means the vendor
loader parses our image the same way it parses the one it shipped with. That is
the difference between a trial boot and a lesson in patience.

usage:
    python scripts/mk_bootimg_violet.py \
        --ramdisk out/qeaf-initramfs.cpio.gz \
        --output out/boot-qeaf-violet-m48.img \
        --append "qeaf.verbose qeaf.nopanel qeaf.umos qeaf.keepbootcmd"

The result is the pre-branding image. Run scripts/strip_android_from_image.py
on it to produce the no-Android image that is actually flashed.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validate_bootimg import validate_file  # noqa: E402

OFF_KERNEL_SIZE = 0x08
OFF_KERNEL_ADDR = 0x0C
OFF_RAMDISK_SIZE = 0x10
OFF_RAMDISK_ADDR = 0x14
OFF_TAGS_ADDR = 0x20
OFF_PAGE_SIZE = 0x24
OFF_CMDLINE = 0x40
CMDLINE_MAX = 512

DEFAULT_REFERENCE = "out/reference/stock-violet/boot.img"
DEFAULT_RAMDISK = "out/qeaf-initramfs.cpio.gz"
DEFAULT_OUTPUT = "out/boot-qeaf-violet.img"


def rd32(page, off):
    return struct.unpack_from("<I", page, off)[0]


def wr32(page, off, val):
    struct.pack_into("<I", page, off, val)


def round_up(size, block):
    return (size + block - 1) // block * block


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--reference", default=DEFAULT_REFERENCE)
    ap.add_argument("--ramdisk", default=DEFAULT_RAMDISK)
    ap.add_argument("--output", default=DEFAULT_OUTPUT)
    ap.add_argument("--cmdline", default=None,
                    help="replace the stock command line; the default keeps it")
    ap.add_argument("--append", default=None,
                    help="append words to the reference command line, e.g. "
                         "the qeaf.* flags; ignored when --cmdline is given")
    ap.add_argument("--ramdisk-addr", type=lambda x: int(x, 0), default=0x01000000,
                    help="load address the loader copies the ramdisk to")
    args = ap.parse_args(argv)

    ref = open(args.reference, "rb").read()
    if ref[:8] != b"ANDROID!":
        sys.stderr.write("mk_bootimg_violet: reference is not an android boot image\n")
        return 1

    page = rd32(ref, OFF_PAGE_SIZE)
    if page not in (2048, 4096, 16384):
        sys.stderr.write("mk_bootimg_violet: reference page size %d is nonsense\n" % page)
        return 1

    kernel_size = rd32(ref, OFF_KERNEL_SIZE)
    kernel = ref[page:page + kernel_size]
    if len(kernel) != kernel_size:
        sys.stderr.write("mk_bootimg_violet: reference is truncated inside the kernel\n")
        return 1

    ramdisk = open(args.ramdisk, "rb").read()
    if not ramdisk:
        sys.stderr.write("mk_bootimg_violet: %s is empty\n" % args.ramdisk)
        return 1

    header = bytearray(ref[:page])
    wr32(header, OFF_RAMDISK_SIZE, len(ramdisk))
    wr32(header, OFF_RAMDISK_ADDR, args.ramdisk_addr)

    cmdline = args.cmdline
    if cmdline is None and args.append:
        stock = bytes(header[OFF_CMDLINE:OFF_CMDLINE + CMDLINE_MAX]).split(b"\0")[0]
        cmdline = (stock.decode("ascii").rstrip() + " " + args.append).strip()
    if cmdline is not None:
        raw = cmdline.encode("ascii")
        if len(raw) >= CMDLINE_MAX:
            sys.stderr.write("mk_bootimg_violet: cmdline is %d bytes, the field holds %d\n"
                             % (len(raw), CMDLINE_MAX - 1))
            return 1
        header[OFF_CMDLINE:OFF_CMDLINE + CMDLINE_MAX] = (
            raw + b"\0" * (CMDLINE_MAX - len(raw)))
    # The stock image carries an AVB hash island and a footer that point at the
    # old section sizes. Keeping them would describe an image we no longer have;
    # the bootloader is unlocked, so the hash is not what gates the boot.

    out = args.output
    tmp = out + ".part"
    with open(tmp, "wb") as f:
        f.write(bytes(header))
        f.write(kernel + b"\0" * (round_up(kernel_size, page) - kernel_size))
        f.write(ramdisk + b"\0" * (round_up(len(ramdisk), page) - len(ramdisk)))
        total = f.tell()
        f.write(b"\0" * (round_up(total, page) - total))
    os.replace(tmp, out)

    check = open(out, "rb").read()
    print("mk_bootimg_violet: page %d, kernel %d, ramdisk %d, image %d bytes"
          % (rd32(check, OFF_PAGE_SIZE), rd32(check, OFF_KERNEL_SIZE),
             rd32(check, OFF_RAMDISK_SIZE), len(check)))
    print("mk_bootimg_violet: kernel_addr %#x ramdisk_addr %#x tags_addr %#x"
          % (rd32(check, OFF_KERNEL_ADDR), rd32(check, OFF_RAMDISK_ADDR),
             rd32(check, OFF_TAGS_ADDR)))
    print("mk_bootimg_violet: cmdline: %s"
          % check[OFF_CMDLINE:OFF_CMDLINE + CMDLINE_MAX].split(b"\0")[0].decode())

    result = validate_file(out)
    if not result.ok:
        sys.stderr.write("mk_bootimg_violet: REFUSED - %s failed validation\n" % out)
        for err in result.errors:
            sys.stderr.write("  ERROR: %s\n" % err)
        return 1
    print("mk_bootimg_violet: [PASS] %s (sha256 %s)"
          % (out, result.facts["sha256"][:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
