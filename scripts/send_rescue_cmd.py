#!/usr/bin/env python3
"""scripts/send_rescue_cmd.py - host -> phone batch writer for QEAFRESCUE.

Replaces the lost build/send-cmd.ps1. Writes one QEAF.CMD (one verb per
line), flushes the volume so Windows commits the directory entry, waits,
then reads RESPONSE.TXT back through scripts/read_rescue_raw.py (raw
sectors; the mounted view is stale on this driver).

HARD RULES baked in here:
  - never ejects and never touches Write-CimInstance/Remove-Volume: an
    eject kills the medium and the channel dies until the cable moves.
  - names must be 8.3; the device's reader skips long-name fragments.
  - every batch must end in a unique nonce line (we append one), because
    an unanswered batch still carries the previous batch's header and
    would read as "done" before the phone started.
  - a verb that WRITES A PARTITION (`flash ...`) needs explicit consent:
    pass --confirm-partition-write. Without it this script refuses before
    touching QEAF.CMD. A `flash` line must also name `recovery` as its only
    target and carry the device's own `confirm` token. p76/boot/userdata
    are never accepted here.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Verbs whose execution writes to a partition rather than just reading state.
PARTITION_WRITE_VERBS = {"flash"}
# The rescue shell only ever writes this one; everything else is refused.
ALLOWED_FLASH_TARGET = "recovery"
DEVICE_CONFIRM_TOKEN = "confirm"


def audit_commands(lines):
    """Return (needs_consent, errors) for a proposed QEAF.CMD batch."""
    needs = False
    errors = []
    for line in lines:
        parts = line.strip().split()
        if not parts:
            continue
        verb = parts[0].lower()
        if verb not in PARTITION_WRITE_VERBS:
            continue
        needs = True
        if len(parts) < 2:
            errors.append("flash verb has no target: %r" % line)
            continue
        target = parts[1].lower()
        if target != ALLOWED_FLASH_TARGET:
            errors.append("refusing to flash %r - only p77/recovery is writable"
                          % parts[1])
        if DEVICE_CONFIRM_TOKEN not in [p.lower() for p in parts[2:]]:
            errors.append("flash line is missing the device's %r token: %r"
                          % (DEVICE_CONFIRM_TOKEN, line))
    return needs, errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--volume", default="F:")
    ap.add_argument("--cmd", action="append", required=True,
                    help="rescue verb, repeatable; executed as one batch")
    ap.add_argument("--wait", type=float, default=12.0,
                    help="seconds to give the phone before reading back")
    ap.add_argument("--out", default="out/report-send",
                    help="directory for the raw read-back")
    ap.add_argument("--confirm-partition-write", "--agree", action="store_true",
                    help="explicit consent required for any `flash ...` verb, "
                         "because it writes to the p77/recovery partition")
    args = ap.parse_args()

    drive = args.volume.rstrip("\\/")
    nonce = "echo %s" % (time.strftime("%H%M%S"))
    lines = list(args.cmd) + [nonce]
    for line in lines:
        if len(line.encode()) > 240:
            print("REFUSED: line too long for one directory entry: %r" % line)
            return 2

    needs_consent, errors = audit_commands(list(args.cmd))
    if errors:
        for e in errors:
            print("REFUSED: %s" % e)
        print("Nothing was written to QEAF.CMD.")
        return 2
    if needs_consent:
        print("PARTITION WRITE: this batch writes to p77/recovery.")
        for line in args.cmd:
            if line.strip().split() and line.strip().split()[0].lower() in PARTITION_WRITE_VERBS:
                print("  %s" % line)
        if not args.confirm_partition_write:
            print("REFUSED: a partition write needs --confirm-partition-write "
                  "(--agree). Nothing was written.")
            return 2
        print("consent: --confirm-partition-write given. Proceeding.")

    target = Path(drive) / "QEAF.CMD"
    with open(target, "wb") as f:
        f.write(("\r\n".join(lines) + "\r\n").encode())

    subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "Write-VolumeCache -DriveLetter %s" % drive.rstrip(":")],
        check=True, capture_output=True, text=True,
    )
    print("batch written and flushed (%d lines + nonce)" % (len(args.cmd)))
    time.sleep(args.wait)

    rc = subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "read_rescue_raw.py"),
         "--drive", drive, "--out", args.out],
    ).returncode
    if rc != 0:
        print("read-back failed rc=%d" % rc)
        return rc
    resp = Path(args.out) / "RESPONSE.TXT"
    print("---- %s ----" % resp)
    data = resp.read_bytes()
    text = data.decode("utf-8", "replace").replace("\x00", "")
    print(text)
    return 0 if nonce.split()[1] in text else 3


if __name__ == "__main__":
    sys.exit(main())
