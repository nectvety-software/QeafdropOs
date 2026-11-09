#!/usr/bin/env python3
"""Fail-loud audit: does a p77 image still contain the rescue channel?

A boot image can pass validate_bootimg.py (ANDROID!, gzip kernel, gzip
ramdisk, sane addresses) and still have NO QEAF.CMD reader, no HELP.TXT and
no RESPONSE.TXT. Rebooting into that image loses the QEAFRESCUE LUN and with
it every host-side management channel. Structural validity is not the same
thing as a working OS.

This check reads the ramdisk itself, so it works on any local image and on a
BOOTIMG.BIN staged on the LUN. Exit 0 = channel present, 1 = absent, 2 = the
file could not be read as a boot image.

    python3 scripts/audit_rescue_channel.py out/boot-qeaf-violet-m48-noandroid.img
"""

import argparse
import gzip
import json
import struct
import sys

MARKERS = {
    "rescue_shell": b"QEAF.CMD",
    "help_text": b"HELP.TXT",
    "response": b"RESPONSE.TXT",
    "flash_verb": b"flash recovery",
    "sync_protocol": b"DEVINFO.TXT",
    # A command loop in RAM is useless if the host cannot SEE the volume.
    # The working OS binds f_mass_storage through configfs; a ramdisk with
    # none of these strings only ever builds a FAT image in private memory.
    "gadget_configfs": b"/config/usb_gadget",
    "gadget_vendor": b"idVendor",
    "gadget_function": b"mass_storage",
    "gadget_udc": b"UDC",
}

REQUIRED = ("rescue_shell", "flash_verb", "gadget_configfs",
            "gadget_vendor", "gadget_function", "gadget_udc",
            "init_panel", "init_gadget")


def cpio_entries(plain):
    """Yield (name, body) from a newc cpio archive.

    The header is 110 bytes (6-byte magic + 13 x 8-byte hex fields) and the
    NAME is padded to a 4-byte boundary RELATIVE TO THE START OF THE ENTRY,
    not relative to the name field. Getting that wrong silently reads the
    wrong bytes - which is how a panel driver in bin/umos was mistaken for
    one in /sbin/qeafinit.
    """
    i = 0
    while i + 110 <= len(plain) and plain[i:i + 6] in (b"070701", b"070702"):
        def fld(j):
            try:
                return int(plain[i + 6 + j * 8:i + 14 + j * 8], 16)
            except ValueError:
                return 0
        namesize, filesize = fld(11), fld(6)
        if namesize < 2 or namesize > 4096 or filesize > len(plain):
            return
        name = plain[i + 110:i + 110 + namesize - 1].decode("utf-8", "replace")
        start = i + ((110 + namesize + 3) & ~3)
        body = plain[start:start + filesize]
        if name == "TRAILER!!!":
            return
        yield name, body
        i = start + ((filesize + 3) & ~3)


def init_binary(plain):
    """Resolve the PID-1 binary. Returns (target, bytes) or ('', b'').

    Two layouts exist and both are valid:
      - UMOS: /init is a text symlink ("/sbin/qeafinit"), follow it.
      - QeafdropOs/Homo: /init IS the ELF (homo-init), use it directly.
    Decoding an ELF body as a symlink path produced a multi-megabyte garbage
    'target', a resolved size of 0, and a false 'missing init_binary' verdict.
    """
    init_body, files = b"", {}
    for name, body in cpio_entries(plain):
        norm = name[2:] if name.startswith("./") else name
        if norm == "init":
            init_body = body
        files[norm] = body
    if not init_body:
        return "", b""
    if init_body[:4] == b"\x7fELF":
        return "init", init_body
    target = init_body.decode("utf-8", "replace").strip("\x00")
    if not target:
        return "", b""
    return target, files.get(target.lstrip("/"), b"")


def audit(path):
    report = {"path": path, "ok": False, "reason": "",
              "markers": {}, "size": 0, "ramdisk_size": 0}
    with open(path, "rb") as fh:
        data = fh.read()
    report["size"] = len(data)
    if len(data) < 4096 or data[:8] != b"ANDROID!":
        report["reason"] = "not an ANDROID! boot image"
        return report
    page = struct.unpack_from("<I", data, 0x24)[0] or 4096
    kernel_size = struct.unpack_from("<I", data, 0x08)[0]
    ramdisk_size = struct.unpack_from("<I", data, 0x10)[0]
    off = page + ((kernel_size + page - 1) // page) * page
    report["ramdisk_size"] = ramdisk_size
    if off + ramdisk_size > len(data):
        report["reason"] = "ramdisk runs past the end of the file"
        return report
    try:
        plain = gzip.decompress(data[off:off + ramdisk_size])
    except OSError as exc:
        report["reason"] = f"ramdisk is not gzip: {exc}"
        return report
    report["markers"] = {k: plain.count(v) for k, v in MARKERS.items()}
    target, init = init_binary(plain)
    report["init_target"] = target
    report["init_size"] = len(init)
    # A marker anywhere in the ramdisk is not evidence. The PID-1 binary is
    # the only thing that can bring up the panel and bind the USB gadget.
    report["markers"]["init_panel"] = (
        init.count(b"SETCRTC") + init.count(b"/dev/fb0") +
        init.count(b"panel: first frame"))
    report["markers"]["init_gadget"] = (
        init.count(b"UDC") + init.count(b"configfs") +
        init.count(b"lun.0/file"))
    missing = [k for k in REQUIRED if not report["markers"].get(k)]
    if not init:
        missing.append("init_binary")
    report["ok"] = not missing
    if missing:
        report["reason"] = (
            "no usable rescue channel (missing " + ", ".join(missing) + ") - "
            "DO NOT REBOOT into this image: QEAFRESCUE would disappear")
        if any(k.startswith("gadget_") for k in missing):
            report["reason"] += (
                "; the command loop exists but no USB mass-storage gadget is "
                "bound, so the host cannot mount the LUN")
        if "init_panel" in missing or "init_gadget" in missing or \
                "init_binary" in missing:
            report["reason"] += (
                "; /init (%s, %d bytes) lacks the DRM panel / configfs gadget "
                "drivers - the phone would show a BLANK SCREEN and no LUN"
                % (target or "unresolved", len(init)))
    return report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="boot image or staged BOOTIMG.BIN")
    ap.add_argument("--json", action="store_true", help="print JSON")
    args = ap.parse_args(argv)
    try:
        report = audit(args.image)
    except OSError as exc:
        print("REFUSED: %s" % exc, file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print("[%s] %s (%d bytes, ramdisk %d)"
              % ("PASS" if report["ok"] else "WARN", report["path"],
                 report["size"], report["ramdisk_size"]))
        for k, v in report["markers"].items():
            print("  %-16s %d" % (k, v))
        if report["reason"]:
            print("  %s" % report["reason"])
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
