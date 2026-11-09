#!/usr/bin/env python3
"""Regression gate for the no-Android p77 image.

scripts/strip_android_from_image.py patches the pre-branding m48 image in
place. This test proves the patch changed NOTHING but equal-length string
replacements (9 strings x 7 bytes = 63 bytes) across the five cpio entries
that carried the word, and that the flash tools actually point at the patched
image.
"""

import contextlib
import gzip
import hashlib
import io
import os
import re
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from validate_bootimg import validate_file  # noqa: E402
import strip_android_from_image as strip  # noqa: E402

SRC = os.path.join(ROOT, "out", "boot-qeaf-violet-m48.img")
DST = os.path.join(ROOT, "out", "boot-qeaf-violet-m48-noandroid.img")
HEADER_FIELDS = {
    "kernel_size": 0x08,
    "kernel_addr": 0x0C,
    "ramdisk_size": 0x10,
    "ramdisk_addr": 0x14,
    "second_size": 0x18,
    "second_addr": 0x1C,
    "tags_addr": 0x20,
    "page_size": 0x24,
    "header_version": 0x28,
    "os_version": 0x2C,
}

_fails = 0
_count = 0


def check(cond, label):
    global _fails, _count
    _count += 1
    if cond:
        print("  ok   %s" % label)
    else:
        _fails += 1
        print("  FAIL %s" % label)


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def cpio_entries(raw):
    off = 0
    while off + 110 <= len(raw):
        if raw[off:off + 6] not in (b"070701", b"070702"):
            break
        fields = [int(raw[off + 6 + i * 8:off + 14 + i * 8], 16)
                  for i in range(13)]
        namesize, filesize = fields[11], fields[6]
        name = raw[off + 110:off + 110 + namesize - 1]
        start = off + 110 + namesize
        start += (4 - (start % 4)) % 4
        end = start + filesize
        yield name, start, end
        off = end + ((4 - (end % 4)) % 4)
        if name == b"TRAILER!!!":
            break


def brand_in(body):
    """Case-insensitive 'android' excluding the ANDROID! protocol token."""
    return sum(1 for m in re.finditer(b"android", body, re.I)
               if body[m.start():m.start() + 8] != b"ANDROID!")


def main():
    print("=== no-Android p77 image regression ===")
    if not os.path.isfile(SRC) or not os.path.isfile(DST):
        print("  FAIL both m48 and the no-Android image must exist")
        return 1
    check(True, "m48 source and no-Android output both exist")

    a = open(SRC, "rb").read()
    b = open(DST, "rb").read()
    data_a = a
    ra = validate_file(SRC)
    rb = validate_file(DST)
    check(ra.ok, "source m48 passes validate_bootimg.py")
    check(rb.ok, "output passes validate_bootimg.py (bootable p77 image)")

    check(a[:8] == b"ANDROID!" and b[:8] == b"ANDROID!",
          "both carry the ANDROID! boot header magic")
    for field, off in HEADER_FIELDS.items():
        if field == "ramdisk_size":
            check(u32(a, off) != u32(b, off),
                  "ramdisk_size changed after re-gzip (expected)")
        else:
            check(u32(a, off) == u32(b, off), "header %s unchanged" % field)
    check(a[0x30:0x660] == b[0x30:0x660],
          "name / cmdline / id / extra_cmdline copied byte-for-byte")

    page = u32(a, 0x24)
    kernel_off = page
    ramdisk_off = page + ((u32(a, 0x08) + page - 1) // page) * page
    check(ramdisk_off == 17510400, "ramdisk sits at the real m48 offset")
    check(a[kernel_off:ramdisk_off] == b[kernel_off:ramdisk_off],
          "kernel blob is byte-identical (%d bytes)" % (ramdisk_off - kernel_off))

    plain_a = gzip.decompress(a[ramdisk_off:ramdisk_off + u32(a, 0x10)])
    plain_b = gzip.decompress(b[ramdisk_off:ramdisk_off + u32(b, 0x10)])
    check(len(plain_a) == len(plain_b),
          "uncompressed ramdisk length unchanged (%d bytes)" % len(plain_a))

    ea = list(cpio_entries(plain_a))
    eb = list(cpio_entries(plain_b))
    check(len(ea) == len(eb) and len(ea) > 200,
          "cpio entry count unchanged (%d entries)" % len(ea))
    check([n for n, _, _ in ea] == [n for n, _, _ in eb],
          "cpio entry names and order unchanged")
    check(ea[-1][0] == eb[-1][0] == b"TRAILER!!!",
          "both archives end with TRAILER!!! (not truncated)")

    branded = {n for n, s, e in ea if re.search(b"Android", plain_a[s:e])}
    changed = [n for (n, s, e), (n2, s2, e2) in zip(ea, eb)
               if plain_a[s:e] != plain_b[s2:e2]]
    check(set(changed) == branded,
          "only the entries that carried the word changed: %s"
          % sorted(x.decode() for x in changed))
    check(branded == {b"./bin/umos", b"./bin/qeaf-report", b"./bin/qeaf-wifi",
                      b"./etc/qeaf/fstab", b"./etc/qeaf/ueventd.cfg"},
          "the five branded entries are the expected ones")

    diffs = [i for i, (x, y) in enumerate(zip(plain_a, plain_b)) if x != y]
    check(len(diffs) == 9 * 7,
          "exactly 63 bytes differ (9 strings x 7 bytes), got %d" % len(diffs))
    branded_ranges = [(s, e) for n, s, e in eb if n in branded]
    check(all(any(s <= i < e for s, e in branded_ranges) for i in diffs),
          "every differing byte is inside a branded entry")
    check(all((plain_a[i], plain_b[i]) in
              {(ord(x), ord(y)) for x, y in zip("Android", "System ")}
              for i in diffs),
          "every difference is an Android->System  character swap")

    before = {n: plain_a[s:e] for n, s, e in ea}
    after = {n: plain_b[s:e] for n, s, e in eb}
    for name in sorted(branded):
        check(after[name].count(b"Android") == 0,
              "%s has no 'Android' string" % name.decode())
        check(brand_in(after[name]) == 0,
              "%s has no case-insensitive brand word" % name.decode())
        check(after[name].count(b"System ") ==
              before[name].count(b"System ") + before[name].count(b"Android"),
              "%s gained exactly one 'System ' per replaced word" % name.decode())
    check(after[b"./bin/umos"].count(b"System ") == 3,
          "./bin/umos carries exactly 3 replacement labels")
    check(after[b"./sbin/qeafinit"] == before[b"./sbin/qeafinit"],
          "./sbin/qeafinit is byte-identical (it never carried the word)")
    check(strip.brand_in_gui(plain_b) == 0,
          "strip_android_from_image.brand_in_gui() agrees: 0")
    check(plain_a.count(b"ANDROID!") == plain_b.count(b"ANDROID!"),
          "ANDROID! boot-magic token count unchanged in the ramdisk")
    check(data_a[:8] == b"ANDROID!" and b[:8] == b"ANDROID!",
          "both carry the ANDROID! boot-header magic at offset 0")

    # The flash path must use the patched image, not the pre-branding source.
    stager = open(os.path.join(ROOT, "scripts", "prepare_rescue_flash.py"),
                  encoding="utf-8").read()
    restore = open(os.path.join(ROOT, "scripts", "restore_p77.ps1"),
                   encoding="utf-8").read()
    check("boot-qeaf-violet-m48-noandroid.img" in stager,
          "prepare_rescue_flash.py defaults to the no-Android image")
    check("boot-qeaf-violet-m48-noandroid.img" in restore,
          "restore_p77.ps1 defaults to the no-Android image")

    # The patch is deterministic and refuses to damage the source.
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "out")) as tmp:
        out2 = os.path.join(tmp, "rebuilt.img")
        with contextlib.redirect_stdout(io.StringIO()):
            rc = strip.main(["--image", SRC, "--output", out2, "--all"])
        rebuilt = open(out2, "rb").read() if rc == 0 else b""
        check(rc == 0 and
              hashlib.sha256(rebuilt).hexdigest() ==
              hashlib.sha256(b).hexdigest(),
              "re-running the patch reproduces the identical image byte-for-byte")

    print("\n%d checks, %d failures" % (_count, _fails))
    return 1 if _fails else 0


if __name__ == "__main__":
    sys.exit(main())
