#!/usr/bin/env python3
"""Regression gate for the 2026-11-04 p77 bootloop.

Checks that scripts/validate_bootimg.py accepts every real violet image, refuses
every known-bad artifact, and refuses each individual way a skeleton packer can
lie. Also checks the two packers and the rescue-flash stager fail closed.
"""

import gzip
import hashlib
import io
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from validate_bootimg import (  # noqa: E402
    P77_SIZE, validate_bytes, validate_file,
)

CHECKS = 0
FAILURES = 0


def check(cond, label):
    global CHECKS, FAILURES
    CHECKS += 1
    if cond:
        print("  ok   %s" % label)
    else:
        FAILURES += 1
        print("  FAIL %s" % label)


def gz(nbytes):
    """A gzip member containing nbytes of zeros (small, decompressible)."""
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=0) as f:
        f.write(b"\x00" * nbytes)
    return buf.getvalue()


KERNEL_SIZE = (1 << 20) + 4096     # just over the 1 MiB minimum, page aligned
RAMDISK_SIZE = 128 << 10
PAGE = 4096


def make_image(mutate=None):
    """A structurally valid synthetic violet image, plus optional mutation."""
    kernel = gz(4096).ljust(KERNEL_SIZE, b"\x00")
    ramdisk = gz(4096).ljust(RAMDISK_SIZE, b"\x00")
    # The v1 header is 1024 bytes but occupies the whole first 4096-byte page.
    h = bytearray(PAGE)
    h[0:8] = b"ANDROID!"
    fields = {
        0x08: KERNEL_SIZE,
        0x0C: 0x00008000,
        0x10: RAMDISK_SIZE,
        0x14: 0x01000000,
        0x18: 0,
        0x1C: 0,
        0x20: 0x00000100,
        0x24: PAGE,
        0x28: 1,
    }
    for off, val in fields.items():
        struct.pack_into("<I", h, off, val)
    cmdline = (b"hardware=qcom androidboot.console=ttyMSM0 "
               b"qeaf.verbose qeaf.nopanel qeaf.umos qeaf.keepbootcmd\x00")
    h[0x4C:0x4C + len(cmdline)] = cmdline
    if mutate:
        mutate(h)
    data = bytes(h) + kernel + ramdisk
    return data


def run(args, cwd=ROOT):
    return subprocess.run([sys.executable] + args, cwd=cwd,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          universal_newlines=True)


def main():
    global CHECKS, FAILURES
    print("== known-good real images ==")
    good = [
        "out/boot-qeaf-violet-m48.img",
        "out/boot-qeaf-violet-m48-noandroid.img",
        "out/reference/recovery-twrp.img",
    ]
    for rel in good:
        path = os.path.join(ROOT, rel)
        if not os.path.isfile(path):
            print("  skip %s (missing)" % rel)
            continue
        r = validate_file(path)
        check(r.ok, "PASS %s%s" % (rel, "" if r.ok else " -> " + "; ".join(r.errors)))

    print("== known-bad real artifacts ==")
    bad = [
        "out/report2/BOOTIMG.BIN",
        "out/report/BOOTIMG.BIN",
    ]
    # build.ps1 quarantines the fake image into out/rejected/; keep testing it
    # there so rebuilding does not silently drop the regression case.
    rejected = os.path.join(ROOT, "out", "rejected")
    if os.path.isdir(rejected):
        quarantined = sorted(
            f for f in os.listdir(rejected) if f.endswith(".img.bad"))
        if quarantined:
            bad.append(os.path.join("out", "rejected", quarantined[-1]))
    for rel in bad:
        path = os.path.join(ROOT, rel)
        if not os.path.isfile(path):
            print("  skip %s (quarantined or missing)" % rel)
            continue
        r = validate_file(path)
        check(not r.ok, "REFUSED %s" % rel)
        check(any("ELF" in e or "magic" in e for e in r.errors),
              "  reason names the ELF/magic defect")

    # out/boot-qeaf-violet.img is a BUILD OUTPUT, not a fixture. Before a build
    # it is quarantined/absent; after `build.ps1 -P77Image <valid>` it is a
    # byte-identical copy of a validated image. It must never be a bare ELF
    # wrapped in ANDROID! -- that was the 2026-11-04 bootloop.
    print("== staging output is never an unvalidated image ==")
    staged = os.path.join(ROOT, "out", "boot-qeaf-violet.img")
    if os.path.isfile(staged):
        r = validate_file(staged)
        check(r.ok, "out/boot-qeaf-violet.img passes validation (real staged image)"
              + ("" if r.ok else " -> " + "; ".join(r.errors)))
        good_hashes = set()
        for rel in ("out/boot-qeaf-violet-m48.img",
                    "out/boot-qeaf-violet-m48-noandroid.img"):
            p = os.path.join(ROOT, rel)
            if os.path.isfile(p):
                good_hashes.add(hashlib.sha256(open(p, "rb").read()).hexdigest())
        check(r.ok and r.facts["sha256"] in good_hashes,
              "staged output is a byte-identical copy of a known-good p77 image")
    else:
        check(True, "out/boot-qeaf-violet.img absent (fake image quarantined)")
        check(True, "no unvalidated staging output present")

    print("== synthetic baseline is genuinely accepted ==")
    base = make_image()
    r = validate_bytes(base, "synthetic-good")
    check(r.ok, "synthetic gzip kernel + gzip ramdisk passes" +
          ("" if r.ok else " -> " + "; ".join(r.errors)))
    check(r.facts.get("kernel_offset") == 4096, "kernel lives at offset 4096")
    check(r.facts.get("ramdisk_offset") == 4096 + KERNEL_SIZE,
          "ramdisk lives at the next page boundary")

    print("== each individual lie is refused ==")
    cases = [
        ("ELF userspace binary as kernel",
         lambda h: None, lambda d: d[:4096] + b"\x7fELF\x02\x01\x01\x00" + d[4104:]),
        ("ramdisk_size = 0", lambda h: struct.pack_into("<I", h, 0x10, 0), None),
        ("page_size = 1024", lambda h: struct.pack_into("<I", h, 0x24, 1024), None),
        ("header_version = 2", lambda h: struct.pack_into("<I", h, 0x28, 2), None),
        ("name = myos (skeleton packer)", lambda h: h.__setitem__(slice(0x3C, 0x42), b"myos\x00\x00"), None),
        ("wrong load addresses", lambda h: (struct.pack_into("<I", h, 0x0C, 0x80000000),
                                            struct.pack_into("<I", h, 0x14, 0x81000000)), None),
        ("missing ANDROID! magic", lambda h: h.__setitem__(slice(0, 8), b"NOPE!!!!"), None),
        ("kernel_size = 0", lambda h: struct.pack_into("<I", h, 0x08, 0), None),
    ]
    for label, mutate, post in cases:
        data = make_image(mutate)
        if post:
            data = post(data)
        r = validate_bytes(data, label)
        check(not r.ok, "REFUSED: %s" % label)

    print("== structural edge cases ==")
    truncated = base[:4096 + KERNEL_SIZE + 128]
    check(not validate_bytes(truncated, "truncated").ok,
          "REFUSED: ramdisk overruns a truncated file")
    oversize = validate_bytes(b"\x00" * (P77_SIZE + 1), "oversize")
    check(not oversize.ok, "REFUSED: file larger than p77")

    print("== packers fail closed ==")
    tmp = os.path.join(ROOT, "out", "tmp")
    os.makedirs(tmp, exist_ok=True)
    forbidden = os.path.join(tmp, "must-not-exist.img")
    if os.path.exists(forbidden):
        os.remove(forbidden)
    p = run(["scripts/pack_bootimg.py", "out/qeafinit.elf", forbidden])
    check(p.returncode == 3, "scripts/pack_bootimg.py refuses qeafinit.elf (exit 3)")
    check(not os.path.exists(forbidden), "no output file was created on refusal")
    check("ELF" in p.stderr, "refusal explains the ELF payload")

    p = run(["build/pack_bootimg.py"])
    check(p.returncode == 3, "build/pack_bootimg.py bare invocation refuses (exit 3)")
    check("VQEAFOS" in p.stderr, "refusal names the historical bad default")

    staged = os.path.join(tmp, "staged.img")
    p = run(["scripts/pack_bootimg.py", "out/boot-qeaf-violet-m48.img", staged])
    check(p.returncode == 0, "packer stages an already-validated p77 image")
    check(os.path.isfile(staged) and
          os.path.getsize(staged) == os.path.getsize(
              os.path.join(ROOT, "out/boot-qeaf-violet-m48.img")),
          "staged copy is byte-size identical")

    print("== rescue-flash stager fails closed ==")
    p = run(["scripts/prepare_rescue_flash.py", "--image", "out/report2/BOOTIMG.BIN"])
    check(p.returncode == 2, "stager refuses the bootloop image")
    check("Nothing was written" in p.stderr, "stager wrote nothing on refusal")
    p = run(["scripts/prepare_rescue_flash.py"])
    check(p.returncode == 0, "stager dry-run accepts the known-good m48 image")
    check("DRY RUN" in p.stdout, "default is a dry run")
    import prepare_rescue_flash as stager  # noqa: E402
    check(stager.cmd_line_is_safe(stager.CMD_LINE),
          "stager's QEAF.CMD names only recovery and carries confirm")
    check(not stager.cmd_line_is_safe("flash recovery BOOTIMG.BIN"),
          "the safety check refuses a flash line without confirm")
    check(not stager.cmd_line_is_safe("flash boot BOOTIMG.BIN confirm"),
          "the safety check refuses a flash to p76/boot")
    check("PARTITION WRITE" in p.stdout,
          "the dry-run plan states that it writes to a partition")

    print("== a partition write needs explicit consent ==")
    import send_rescue_cmd as src  # noqa: E402
    needs, errs = src.audit_commands(["ls /Themes", "echo ok"])
    check(not needs and not errs, "read-only verbs need no consent")
    needs, errs = src.audit_commands(["flash recovery BOOTIMG.BIN confirm"])
    check(needs and not errs,
          "flash recovery + confirm is a valid partition write")
    needs, errs = src.audit_commands(["flash recovery BOOTIMG.BIN"])
    check(needs and errs, "a flash line without the confirm token is refused")
    needs, errs = src.audit_commands(["flash boot BOOTIMG.BIN confirm"])
    check(needs and errs, "flashing p76/boot is refused")
    needs, errs = src.audit_commands(["flash userdata BOOTIMG.BIN confirm"])
    check(needs and errs, "flashing userdata is refused")
    p = run(["scripts/send_rescue_cmd.py", "--cmd",
             "flash recovery BOOTIMG.BIN confirm", "--wait", "0"])
    check(p.returncode == 2,
          "send_rescue_cmd.py refuses a flash without --confirm-partition-write")
    check("Nothing was written" in p.stdout,
          "the refusal leaves QEAF.CMD untouched")
    p = run(["scripts/send_rescue_cmd.py", "--cmd",
             "flash boot BOOTIMG.BIN confirm", "--wait", "0",
             "--confirm-partition-write"])
    check(p.returncode == 2,
          "even with consent, a non-recovery flash target is refused")

    print("== rescue-channel audit (structural validity is not enough) ==")
    import importlib.util  # noqa: E402
    spec = importlib.util.spec_from_file_location(
        "audit_rescue_channel",
        os.path.join(ROOT, "scripts", "audit_rescue_channel.py"))
    audit_mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit_mod)
    prod = audit_mod.audit(os.path.join(ROOT, "out",
                                        "boot-qeaf-violet-m48-noandroid.img"))
    check(prod["ok"] is False and "gadget_configfs" in prod["reason"],
          "the production image is REFUSED: no USB mass-storage gadget bound")
    check(prod["ok"] is False and "BLANK SCREEN" in prod["reason"],
          "and REFUSED for the missing DRM panel driver too")
    check(prod["init_target"] == "sbin/qeafinit" and
          prod["init_size"] == 747560,
          "the audit resolves /init to the REAL PID-1 binary")
    check(prod["markers"]["rescue_shell"] > 0 and
          prod["markers"]["init_panel"] == 0,
          "a panel driver in bin/umos cannot masquerade as one in /init")
    # A known-good image from before the rewrite really does have both.
    good_old = os.path.join(ROOT, "out", "report-raw2", "BOOTIMG.BIN")
    if os.path.isfile(good_old):
        old = audit_mod.audit(good_old)
        check(old["ok"] is True and old["markers"]["init_panel"] > 0 and
              old["markers"]["init_gadget"] > 0,
              "the known-good pre-rewrite image passes the stricter audit")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "out")) as tmp:
        def build(name, payload):
            path = os.path.join(tmp, name)
            kernel = gz(KERNEL_SIZE)
            ramdisk = gzip.compress(payload, compresslevel=6, mtime=0)
            page = 4096
            header = bytearray(page)
            header[0:8] = b"ANDROID!"
            struct.pack_into("<I", header, 0x08, len(kernel))
            struct.pack_into("<I", header, 0x10, len(ramdisk))
            struct.pack_into("<I", header, 0x24, page)
            with open(path, "wb") as fh:
                fh.write(bytes(header) + kernel +
                         b"\x00" * ((-len(kernel)) % page) + ramdisk +
                         b"\x00" * ((-len(ramdisk)) % page))
            return path

        def cpio_entry(name, body, mode=0o100644):
            fields = [1, mode, 0, 0, 1, 0, len(body), 0, 0, 0, 0,
                      len(name) + 1, 0]
            out = b"070701" + b"".join(b"%08X" % v for v in fields)
            out += name.encode() + b"\x00"
            out += b"\x00" * ((-len(out)) % 4)
            out += body
            return out + b"\x00" * ((-len(out)) % 4)

        def make_cpio(init_body):
            return (cpio_entry("./init", b"sbin/qeafinit", 0o120777) +
                    cpio_entry("./sbin/qeafinit", init_body, 0o100755) +
                    cpio_entry("TRAILER!!!", b""))

        # Same structure, no channel: the audit must still refuse it.
        bad = audit_mod.audit(build("no-channel.img",
                                    b"070701" + b"plain initramfs\n" * 8))
        check(bad["ok"] is False and "DO NOT REBOOT" in bad["reason"],
              "a structurally valid image without QEAF.CMD is refused")
        check(bad["markers"]["rescue_shell"] == 0,
              "the audit measures the ramdisk, not the file name")
        # A fake root that carries the markers but NOT in /init must still be
        # refused - this is the bin/umos masquerade that caused the incident.
        decoy = (cpio_entry("./init", b"sbin/qeafinit", 0o120777) +
                 cpio_entry("./sbin/qeafinit", b"plain init, no drivers",
                            0o100755) +
                 cpio_entry("./bin/umos",
                            b"QEAF.CMD flash recovery /config/usb_gadget "
                            b"idVendor mass_storage UDC lun.0/file "
                            b"SETCRTC panel: first frame") +
                 cpio_entry("TRAILER!!!", b""))
        bad2 = audit_mod.audit(build("decoy.img", decoy))
        check(bad2["ok"] is False and bad2["markers"]["init_panel"] == 0,
              "drivers in a NON-init binary do not fool the audit")
        good = audit_mod.audit(build("with-channel.img", make_cpio(
            b"QEAF.CMD reader\nflash recovery BOOTIMG.BIN confirm\n"
            b"HELP.TXT RESPONSE.TXT DEVINFO.TXT\n"
            b"/config/usb_gadget/qeaf/idVendor mass_storage UDC "
            b"lun.0/file SETCRTC panel: first frame\n")))
        check(good["ok"] is True and good["markers"]["rescue_shell"] > 0,
              "an image whose /init really has both drivers passes")

    print("\n%d checks, %d failures" % (CHECKS, FAILURES))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
