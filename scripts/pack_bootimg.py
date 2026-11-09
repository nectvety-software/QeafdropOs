#!/usr/bin/env python3
"""Fail-closed boot image front-end for the violet p77 slot.

This file used to wrap ``qeafinit.elf`` (a userspace ELF) in an ``ANDROID!``
header with ``page_size=1024`` and ``ramdisk_size=0``. That is not a bootable
kernel: it is an ELF where the bootloader expects a gzip Linux image, and there
is no ramdisk to mount. The rescue OS only checked the 8-byte magic, so such an
image could be flashed and verified successfully -- see ``out/report2`` for the
17:36 flash on 2026-11-04 that left the phone rebooting continuously.

It now does one of two things:

  * given an ELF (the old input): REFUSE, write nothing, exit 3;
  * given an already-built boot image: validate it structurally, and only then
    copy it to the output path.

A real p77 install image is built from ``out/reference/stock-violet/boot.img``
plus a gzip kernel and an initramfs: ``scripts/mk_initramfs.py`` then
``scripts/mk_bootimg_violet.py``. The current image is
``out/boot-qeaf-violet-m48-noandroid.img`` and must be staged with
``scripts/prepare_rescue_flash.py``.
"""

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validate_bootimg import (BOOT_MAGIC, ELF_MAGIC, validate_file)  # noqa: E402


def refuse(kernel, output, detail):
    print("REFUSED: will not create %s" % output, file=sys.stderr)
    print("  input : %s" % kernel, file=sys.stderr)
    print("  reason: %s" % detail, file=sys.stderr)
    print("", file=sys.stderr)
    print("  An ANDROID! header around a userspace ELF is NOT a boot image.",
          file=sys.stderr)
    print("  The rescue OS only checks the magic, so it would flash this and",
          file=sys.stderr)
    print("  verify it byte-for-byte -- then the phone bootloops on the next",
          file=sys.stderr)
    print("  boot-recovery. See out/report2/RESPONSE.TXT (2026-11-04 17:36).",
          file=sys.stderr)
    print("  Use scripts/prepare_rescue_flash.py with a validated install image.",
          file=sys.stderr)
    if os.path.isfile(output):
        print("  NOTE: stale output %s still exists; build.ps1 quarantines it."
              % output, file=sys.stderr)
    return 3


def main(argv):
    kernel = argv[1] if len(argv) > 1 else "out/qeafinit.elf"
    output = argv[2] if len(argv) > 2 else "out/boot-qeaf-violet.img"

    if not os.path.isfile(kernel):
        return refuse(kernel, output, "input file does not exist")

    with open(kernel, "rb") as f:
        head = f.read(8)

    if head[:4] == ELF_MAGIC:
        return refuse(kernel, output,
                      "input is an ELF userspace binary (%s)" % head.hex())

    if head != BOOT_MAGIC:
        return refuse(kernel, output,
                      "input is neither a validated boot image nor a recognised "
                      "ELF (magic %r)" % head)

    report = validate_file(kernel)
    if not report.ok:
        print("REFUSED: %s is not a bootable violet p77 image" % kernel,
              file=sys.stderr)
        for err in report.errors:
            print("  ERROR: %s" % err, file=sys.stderr)
        return 3

    shutil.copyfile(kernel, output)
    print("Staged validated boot image: %s -> %s (%d bytes, sha256 %s)"
          % (kernel, output, report.facts["size"], report.facts["sha256"][:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
