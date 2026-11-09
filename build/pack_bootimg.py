#!/usr/bin/env python3
"""Fail-closed guard for the second (older) boot packer.

This is the script that actually produced the 97280-byte ``BOOTIMG.BIN`` found
in ``out/report2``: it defaulted to ``D:/QeafivelsOfficial/VQEAFOS/out/kernel.elf``
(an x86-64 ELF, 95296 bytes) and wrote an ``ANDROID!`` header with
``kernel_addr=0x80008000``, ``page_size=1024`` and ``ramdisk_size=0``. The
payload bytes of that BOOTIMG.BIN are byte-for-byte identical to that ELF. It
was flashed to p77 at 17:36 on 2026-11-04 and the phone has reboot-looped ever
since.

It now refuses any ELF input and delegates structural validation to
``scripts/validate_bootimg.py``. Kept as a separate file because old notes and
scripts still call this path; a silent deletion would let them recreate the bug.
"""

import os
import shutil
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "scripts"))
from validate_bootimg import BOOT_MAGIC, ELF_MAGIC, validate_file  # noqa: E402

OLD_INPUT = "D:/QeafivelsOfficial/VQEAFOS/out/kernel.elf"
OLD_OUTPUT = "D:/QeafivelsOfficial/VQEAFOS/out/boot-qeaf-p77-myos.img"


def refuse(kernel, output, detail):
    print("REFUSED: will not create %s" % output, file=sys.stderr)
    print("  input : %s" % kernel, file=sys.stderr)
    print("  reason: %s" % detail, file=sys.stderr)
    print("  This exact path produced the 2026-11-04 17:36 bootloop image",
          file=sys.stderr)
    print("  (out/report2/BOOTIMG.BIN, flashed and verified to p77).",
          file=sys.stderr)
    print("  Use scripts/prepare_rescue_flash.py with a validated p77 image.",
          file=sys.stderr)
    return 3


def main(argv):
    # A bare invocation must never fall back to the historical x86 ELF default.
    if len(argv) < 3:
        return refuse(OLD_INPUT + " (old default)", OLD_OUTPUT,
                      "bare invocation has no validated source image")
    kernel, output = argv[1], argv[2]

    if not os.path.isfile(kernel):
        return refuse(kernel, output, "input file does not exist")
    with open(kernel, "rb") as f:
        head = f.read(8)
    if head[:4] == ELF_MAGIC:
        return refuse(kernel, output,
                      "input is an ELF userspace binary (%s)" % head.hex())
    if head != BOOT_MAGIC:
        return refuse(kernel, output, "missing ANDROID! boot magic (%r)" % head)

    r = validate_file(kernel)
    if not r.ok:
        print("REFUSED: %s is not a bootable violet p77 image" % kernel,
              file=sys.stderr)
        for err in r.errors:
            print("  ERROR: %s" % err, file=sys.stderr)
        return 3

    shutil.copyfile(kernel, output)
    print("Staged validated boot image: %s -> %s (%d bytes, sha256 %s)"
          % (kernel, output, r.facts["size"], r.facts["sha256"][:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
