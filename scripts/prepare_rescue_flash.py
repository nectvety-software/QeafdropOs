#!/usr/bin/env python3
"""Stage a validated p77 image onto the QEAFRESCUE volume.

The 2026-11-04 bootloop happened because a ``BOOTIMG.BIN`` was copied onto the
rescue volume with no structural check: the device-side ``flash`` verb only
verifies the ``ANDROID!`` magic, the word ``confirm``, the target ``recovery``
and the partition size (see ``out/report2/HELP.TXT``). It then wrote the file
and verified it byte-for-byte. Nothing was wrong with the write; the payload
was wrong.

This script is the missing host-side half of that guard. It:

  1. runs ``scripts/validate_bootimg.py`` on the candidate and REFUSES if it is
     not a real gzip-kernel + gzip-ramdisk violet image;
  2. refuses to write to anything that does not look like a QEAFRESCUE volume;
  3. only with ``--commit`` copies to the 8.3 name ``BOOTIMG.BIN``, re-reads it
     and compares SHA-256, then writes a single-line ``QEAF.CMD``.

Default is a dry run: it prints the plan and writes nothing. It never ejects
the volume and never writes to a partition -- it only writes files on the LUN.
"""

import argparse
import hashlib
import importlib.util
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validate_bootimg import P77_SIZE, validate_file  # noqa: E402

# The current OS (m48): stock violet kernel + DTB, the storage init that builds
# QEAFRESCUE with its five folders, and a stage that carries the 66 .vqeaf
# themes. scripts/strip_android_from_image.py removed the brand word from every
# cpio entry; only equal-length replacements were made, so no offset moved.
GOOD_IMAGE = "out/boot-qeaf-violet-m48-noandroid.img"

# QEAF.CMD is an instruction the phone EXECUTES. "flash recovery" writes to
# the p77 partition, so the device's own protocol demands a trailing confirm
# token and this host side must never generate a command without one. --commit
# is the human's consent; there is no second, quieter way to reach the write.
PARTITION_TARGET = "recovery"
CONFIRM_TOKEN = "confirm"
CMD_LINE = "flash %s BOOTIMG.BIN %s" % (PARTITION_TARGET, CONFIRM_TOKEN)
VOLUME_SIGNATURES = ("HELP.TXT", "QEAFBOOT.LOG")


def cmd_line_is_safe(line):
    """A partition write must name only recovery and carry the confirm token."""
    parts = line.strip().split()
    return (len(parts) >= 4 and parts[0] == "flash" and
            parts[1] == PARTITION_TARGET and CONFIRM_TOKEN in parts[2:])


def audit_rescue_channel(image):
    """Return the channel audit, or None when the helper is unavailable."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "audit_rescue_channel.py")
    spec = importlib.util.spec_from_file_location("audit_rescue_channel", path)
    if not spec or not spec.loader:
        return None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    try:
        return mod.audit(image)
    except OSError:
        return None


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def looks_like_rescue_volume(volume):
    if not os.path.isdir(volume):
        return False, "%s is not a directory" % volume
    found = [n for n in VOLUME_SIGNATURES if os.path.isfile(os.path.join(volume, n))]
    if not found:
        return False, ("no %s found - this does not look like a QEAFRESCUE LUN"
                       % " / ".join(VOLUME_SIGNATURES))
    return True, ",".join(found)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--image", default=GOOD_IMAGE,
                   help="p77 install image to stage (default: %(default)s)")
    p.add_argument("--volume", default=None,
                   help="QEAFRESCUE volume, e.g. F:\\ (required with --commit)")
    p.add_argument("--commit", action="store_true",
                   help="actually write BOOTIMG.BIN and QEAF.CMD (default: dry run)")
    args = p.parse_args(argv)

    if not cmd_line_is_safe(CMD_LINE):
        print("REFUSED: QEAF.CMD would write a partition without a valid "
              "target or confirm token: %r" % CMD_LINE, file=sys.stderr)
        return 2

    if not os.path.isfile(args.image):
        print("REFUSED: image not found: %s" % args.image, file=sys.stderr)
        return 2
    r = validate_file(args.image)
    print("[%s] %s (%d bytes, sha256 %s)"
          % ("PASS" if r.ok else "REFUSED", r.path, r.facts["size"],
             r.facts["sha256"][:16]))
    for e in r.errors:
        print("  ERROR: %s" % e, file=sys.stderr)
    for w in r.warnings:
        print("  warn : %s" % w)
    if not r.ok:
        print("Nothing was written. Do NOT put this file on QEAFRESCUE.",
              file=sys.stderr)
        return 2

    src_sha = r.facts["sha256"]
    print("  page=%d kernel=%d @0x%08X ramdisk=%d @0x%08X"
          % (r.facts["page_size"], r.facts["kernel_size"], r.facts["kernel_addr"],
             r.facts["ramdisk_size"], r.facts["ramdisk_addr"]))
    print("  plan : copy -> BOOTIMG.BIN ; QEAF.CMD = %r" % CMD_LINE)
    print("  PARTITION WRITE: this command writes p77/recovery.")
    print("  --commit is your explicit consent; without it nothing is written.")
    print("  size : %d / %d bytes of p77" % (r.facts["size"], P77_SIZE))

    channel = audit_rescue_channel(args.image)
    if channel is not None and not channel["ok"]:
        print("  *** WARNING *********************************************")
        print("  %s" % channel["reason"])
        print("  The image is structurally bootable but the next reboot would")
        print("  lose the QEAFRESCUE LUN. Fix the initramfs before rebooting.")
        print("  *************************************************")

    if not args.commit:
        print("\nDRY RUN - nothing written. Re-run with --commit "
              "--volume <QEAFRESCUE drive> once the LUN is mounted.")
        return 0

    if not args.volume:
        print("REFUSED: --commit needs --volume", file=sys.stderr)
        return 2
    ok, why = looks_like_rescue_volume(args.volume)
    if not ok:
        print("REFUSED: %s" % why, file=sys.stderr)
        return 2
    print("  volume signature: %s (%s)" % (args.volume, why))

    volume = args.volume
    real = os.path.realpath(volume)
    if len(real) <= 3 and real.rstrip("\\/").endswith(":"):
        # A bare drive root is fine for a removable LUN, but never allow C:.
        if real.upper().startswith("C:"):
            print("REFUSED: refusing to write to the system drive", file=sys.stderr)
            return 2

    dst = os.path.join(volume, "BOOTIMG.BIN")
    shutil.copyfile(args.image, dst)
    dst_sha = sha256(dst)
    if dst_sha != src_sha:
        print("REFUSED: copy mismatch src=%s dst=%s - remove BOOTIMG.BIN "
              "before retrying" % (src_sha[:16], dst_sha[:16]), file=sys.stderr)
        return 2
    print("  wrote %s (%d bytes, sha256 verified)" % (dst, os.path.getsize(dst)))

    cmd_path = os.path.join(volume, "QEAF.CMD")
    with open(cmd_path, "w", newline="\n") as f:
        f.write(CMD_LINE + "\n")
    print("  wrote %s (one line: %s)" % (cmd_path, CMD_LINE))

    print("\nNEXT (do not eject):")
    print("  1. flush the volume cache (Write-VolumeCache / sync), never -Eject")
    print("  2. wait ~10 s, then read RESPONSE.TXT by RAW SECTOR, not the "
          "mounted view")
    print("  3. require BOTH proofs: 'flash: written and verified byte for "
          "byte' in RESPONSE.TXT and 'lun: verified - recovery and BOOTIMG.BIN "
          "agree byte for byte over %d bytes' in QEAFBOOT.LOG"
          % r.facts["size"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
