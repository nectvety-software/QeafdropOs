#!/usr/bin/env python3
"""Host-side structural validation for a violet p77 / recovery boot image.

Why this file exists
--------------------
On 2026-11-04 at 17:36 a 97280-byte ``BOOTIMG.BIN`` was flashed to p77 and
verified byte-for-byte. It had ``ANDROID!`` magic, so it passed every guard the
rescue OS had (target is recovery, ``confirm`` present, magic present, payload
fits the partition). But its "kernel" was ``VQEAFOS/out/kernel.elf`` -- an
x86-64 ELF userspace binary -- with ``page_size=1024``, ``ramdisk_size=0`` and
``kernel_addr=0x80008000``. On the next ``boot-recovery`` the bootloader tried
to execute it, failed, and the phone started rebooting continuously.

The bootloader only sees a header. This module checks the *body* against the
invariants of every real image in ``out/`` (the m48 violet image,
``out/reference/stock-violet/boot.img``, TWRP recovery):

    page_size   = 4096
    kernel      = gzip member at offset 4096, >= 1 MiB
    ramdisk     = non-empty gzip member at the next page boundary
    addresses   = (0x00008000, 0x01000000)  [QEFA install images]
                  or (0x80008000, 0x81000000) [stock/TWRP style]
    file        <= 67108864 bytes (p77 size)

It is deliberately strict and fail-closed: a file is either a bootable violet
image or it is refused. Nothing here writes anything.
"""

import hashlib
import json
import os
import struct
import sys
import zlib

BOOT_MAGIC = b"ANDROID!"
ELF_MAGIC = b"\x7fELF"
GZIP_MAGIC = b"\x1f\x8b\x08"

P77_SIZE = 67108864          # /dev/mmcblk0p77 on violet
PAGE_SIZE_EXPECTED = 4096
HEADER_VERSION_EXPECTED = 1
MIN_KERNEL_SIZE = 1 << 20    # 1 MiB: violet kernels are 13..18 MiB
MIN_RAMDISK_SIZE = 64 << 10  # 64 KiB: violet ramdisks are 1..20 MiB
MIN_FILE_SIZE = 1024

# (kernel_addr, ramdisk_addr) pairs observed on real violet images.
ADDRESS_PAIRS = {
    (0x00008000, 0x01000000),  # out/boot-qeaf-violet-m48*.img
    (0x80008000, 0x81000000),  # out/reference/recovery-twrp.img
}

# name[16] values emitted by the two skeleton packers that produced bad images.
SKELETON_NAMES = {b"myos", b"violet"}


class Report:
    def __init__(self, path):
        self.path = path
        self.ok = True
        self.errors = []
        self.warnings = []
        self.facts = {}

    def error(self, msg):
        self.ok = False
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)


def _align(n, page):
    return ((n + page - 1) // page) * page


def _gzip_member_ok(data, offset, length):
    """A gzip stream that really starts and produces output, not just a magic.

    Only the first 1 MiB of compressed bytes is fed; that is enough to prove the
    payload is a gzip member (an ELF or zero padding raises immediately) without
    decompressing a whole 17 MiB kernel on every build.
    """
    if offset < 0 or length <= 0 or offset + 3 > len(data):
        return False
    chunk = data[offset:min(offset + length, offset + (1 << 20), len(data))]
    if chunk[:3] != GZIP_MAGIC:
        return False
    try:
        dec = zlib.decompressobj(31)  # 31 = gzip wrapper, automatic header
        out = dec.decompress(chunk)
        return len(out) > 0
    except zlib.error:
        return False


def parse_header(data):
    """Return the v1 Android boot header fields. Raises ValueError on nonsense."""
    if len(data) < 0x240:
        raise ValueError("file is shorter than a v1 boot header (576 bytes)")
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    return {
        "magic": bytes(data[0:8]),
        "kernel_size": u32(0x08),
        "kernel_addr": u32(0x0C),
        "ramdisk_size": u32(0x10),
        "ramdisk_addr": u32(0x14),
        "second_size": u32(0x18),
        "second_addr": u32(0x1C),
        "tags_addr": u32(0x20),
        "page_size": u32(0x24),
        "header_version": u32(0x28),
        "name": bytes(data[0x3C:0x4C]).split(b"\x00")[0],
        "cmdline": bytes(data[0x4C:0x24C]).split(b"\x00")[0].decode("ascii", "replace"),
    }


def validate_bytes(data, path="<memory>"):
    """Validate boot image bytes. Returns a Report. Never raises on bad data."""
    r = Report(path)
    r.facts["size"] = len(data)
    r.facts["sha256"] = hashlib.sha256(data).hexdigest()

    if len(data) < MIN_FILE_SIZE:
        r.error("file is %d bytes, too small to be a boot image" % len(data))
        return r
    if data[:8] != BOOT_MAGIC:
        r.error("missing ANDROID! boot magic (got %r)" % bytes(data[:8]))
        return r
    if len(data) > P77_SIZE:
        r.error("file is %d bytes, larger than p77 (%d bytes)" % (len(data), P77_SIZE))

    try:
        h = parse_header(data)
    except ValueError as exc:
        r.error(str(exc))
        return r
    r.facts.update(h)

    if h["header_version"] != HEADER_VERSION_EXPECTED:
        r.error("header_version=%d, violet p77 images are v1" % h["header_version"])
    if h["page_size"] != PAGE_SIZE_EXPECTED:
        r.error("page_size=%d, expected %d (skeleton packers use 1024)"
                % (h["page_size"], PAGE_SIZE_EXPECTED))
    if h["kernel_size"] < MIN_KERNEL_SIZE:
        r.error("kernel_size=%d, implausibly small (< %d)"
                % (h["kernel_size"], MIN_KERNEL_SIZE))
    if h["ramdisk_size"] < MIN_RAMDISK_SIZE:
        r.error("ramdisk_size=%d, a bootable recovery needs a non-empty ramdisk"
                % h["ramdisk_size"])
    if (h["kernel_addr"], h["ramdisk_addr"]) not in ADDRESS_PAIRS:
        r.error("kernel_addr=0x%08X / ramdisk_addr=0x%08X do not match any real "
                "violet image" % (h["kernel_addr"], h["ramdisk_addr"]))
    if h["name"] in SKELETON_NAMES:
        r.error("name=%r is the signature of the skeleton ELF packer, not a "
                "real boot image" % h["name"])
    if "hardware=qcom" not in h["cmdline"]:
        r.warn("cmdline has no 'hardware=qcom' (stock violet cmdline does)")

    page = h["page_size"] if h["page_size"] else PAGE_SIZE_EXPECTED
    kernel_off = page
    ramdisk_off = page + _align(h["kernel_size"], page)
    r.facts["kernel_offset"] = kernel_off
    r.facts["ramdisk_offset"] = ramdisk_off

    # --- kernel body -------------------------------------------------------
    if kernel_off + 8 > len(data):
        r.error("kernel payload starts at %d but file ends at %d"
                % (kernel_off, len(data)))
    else:
        head = bytes(data[kernel_off:kernel_off + 8])
        r.facts["kernel_magic"] = head.hex()
        if head[:4] == ELF_MAGIC:
            r.error("kernel payload is an ELF userspace binary (%s), not a Linux "
                    "kernel -- this is exactly the 2026-11-04 17:36 bootloop image"
                    % head.hex())
        elif not _gzip_member_ok(data, kernel_off, h["kernel_size"]):
            r.error("kernel payload at %d is not a decompressible gzip member "
                    "(magic %s)" % (kernel_off, head[:4].hex()))
        if kernel_off + h["kernel_size"] > len(data):
            r.error("kernel_size=%d overruns the file (%d bytes)"
                    % (h["kernel_size"], len(data)))

    # --- ramdisk body ------------------------------------------------------
    if h["ramdisk_size"]:
        if ramdisk_off + 8 > len(data):
            r.error("ramdisk payload starts at %d but file ends at %d"
                    % (ramdisk_off, len(data)))
        else:
            head = bytes(data[ramdisk_off:ramdisk_off + 8])
            r.facts["ramdisk_magic"] = head.hex()
            if not _gzip_member_ok(data, ramdisk_off, h["ramdisk_size"]):
                r.error("ramdisk payload at %d is not a decompressible gzip "
                        "member (magic %s)" % (ramdisk_off, head[:4].hex()))
            if ramdisk_off + h["ramdisk_size"] > len(data):
                r.error("ramdisk_size=%d overruns the file (%d bytes)"
                        % (h["ramdisk_size"], len(data)))

    if h["second_size"]:
        r.warn("second_size=%d (real violet images leave this zero)"
               % h["second_size"])
    return r


def validate_file(path):
    with open(path, "rb") as f:
        return validate_bytes(f.read(), path)


def _print_report(r, quiet=False):
    verdict = "PASS" if r.ok else "REFUSED"
    print("[%s] %s (%d bytes, sha256 %s)"
          % (verdict, r.path, r.facts.get("size", 0), r.facts.get("sha256", "")[:16]))
    if not quiet:
        h = r.facts
        if "kernel_size" in h:
            print("    page=%s kernel=%s @0x%08X ramdisk=%s @0x%08X name=%r"
                  % (h.get("page_size"), h.get("kernel_size"),
                     h.get("kernel_addr", 0), h.get("ramdisk_size"),
                     h.get("ramdisk_addr", 0), h.get("name", b"")))
        for w in r.warnings:
            print("    warn : %s" % w)
    for e in r.errors:
        print("    ERROR: %s" % e)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    quiet = "--quiet" in argv
    as_json = "--json" in argv
    if not args:
        print(__doc__)
        print("usage: python scripts/validate_bootimg.py [--quiet|--json] IMAGE [IMAGE ...]")
        return 2
    reports = []
    for path in args:
        if not os.path.isfile(path):
            print("[REFUSED] %s (no such file)" % path)
            reports.append(False)
            continue
        r = validate_file(path)
        if as_json:
            print(json.dumps({
                "path": r.path, "ok": r.ok, "errors": r.errors,
                "warnings": r.warnings,
                "facts": {k: (v.hex() if isinstance(v, bytes) else v)
                          for k, v in r.facts.items()},
            }, indent=2))
        else:
            _print_report(r, quiet)
        reports.append(r.ok)
    return 0 if reports and all(reports) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
