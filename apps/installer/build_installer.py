#!/usr/bin/env python3
"""Build the QeafdropOs installer app and package it as a .qeapp.

Usage:
    python build_installer.py             # build + package
    python build_installer.py --drive F:  # build + package + copy to QEAFRESCUE LUN
"""
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
QEAF = os.path.dirname(os.path.dirname(ROOT))  # apps/installer → QeafdropOs root
OUT  = os.path.join(QEAF, "out")
ZIG  = os.path.join(QEAF, "out", "toolchain", "zig-windows-x86_64-0.13.0", "zig.exe")

TARGET_NAME = "installer"
TARGET      = os.path.join(ROOT, TARGET_NAME)
PACKAGE     = os.path.join(OUT, "installer.qeapp")


def cross_compile():
    """Cross-compile main.c → static aarch64-musl ELF."""
    cmd = [
        ZIG, "cc",
        "-target", "aarch64-linux-musl",
        "-O2", "-static", "-lm",
        "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-D_GNU_SOURCE",
        "-I" + os.path.join(QEAF),
        os.path.join(ROOT, "main.c"),
        "-o", TARGET,
    ]
    print("build_installer:", " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.stdout:
        sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit("build_installer: compile failed (exit %d)" % result.returncode)

    # verify it's an aarch64 ELF
    with open(TARGET, "rb") as f:
        head = f.read(20)
    if head[:4] != b"\x7fELF" or head[4] != 2:
        raise SystemExit("build_installer: %s is not a 64-bit ELF" % TARGET)
    machine = struct.unpack_from("<H", head, 18)[0]
    if machine != 0xB7:
        raise SystemExit("build_installer: %s is not aarch64 (machine=0x%04x)" % (TARGET, machine))

    size = os.path.getsize(TARGET)
    print("build_installer: %s (%d bytes, aarch64-musl ELF)" % (TARGET, size))
    return size


def package(size_bytes):
    """Package the binary into a .qeapp (zip with VQEAF-QEAPP/1 manifest)."""
    staging = os.path.join(OUT, "installer-pkg")
    shutil.rmtree(staging, ignore_errors=True)
    os.makedirs(staging, exist_ok=True)

    # copy the binary
    shutil.copy2(TARGET, os.path.join(staging, TARGET_NAME))
    os.chmod(os.path.join(staging, TARGET_NAME), 0o755)

    # manifest
    manifest = "VQEAF-QEAPP/1\n"
    manifest += "name=QeafdropOs Installer\n"
    manifest += "id=com.qeaf.installer\n"
    manifest += "version=1.0.0\n"
    manifest += "category=applications\n"
    manifest += "developer=Qeaf Software\n"
    manifest += "size_kb=%d\n" % ((size_bytes + 1023) // 1024)
    manifest += "icon=icon.png\n"
    with open(os.path.join(staging, "META-INF.QEA"), "w", encoding="utf-8") as f:
        f.write(manifest)

    # minimal 1x1 PNG (valid PNG header + IHDR + IDAT + IEND)
    png_1x1 = bytes([
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
        0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41,
        0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x72, 0x60, 0x1A,
        0x56, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42, 0x60, 0x82,
    ])
    with open(os.path.join(staging, "icon.png"), "wb") as f:
        f.write(png_1x1)

    # zip it
    shutil.make_archive(PACKAGE.replace(".qeapp", ""), "zip", staging)
    # rename .zip → .qeapp
    zip_path = PACKAGE.replace(".qeapp", ".zip")
    if os.path.exists(PACKAGE):
        os.remove(PACKAGE)
    os.rename(zip_path, PACKAGE)

    pkg_size = os.path.getsize(PACKAGE)
    print("build_installer: %s (%d bytes)" % (PACKAGE, pkg_size))
    return pkg_size


def install_on_lun(drive):
    if drive and os.path.exists(drive):
        dest = os.path.join(drive, "installer.qeapp")
        shutil.copy2(PACKAGE, dest)
        print("build_installer: copied to %s" % dest)
    elif drive:
        print("build_installer: drive %s not found" % drive)


if __name__ == "__main__":
    size = cross_compile()
    package(size)
    drive = None
    for a in sys.argv[1:]:
        if a.startswith("--drive="):
            drive = a.split("=", 1)[1]
        elif a.startswith("--drive"):
            drive = a.split(" ", 1)[1] if " " in a else None
        elif a.startswith("-d"):
            drive = a[2:]
    if drive:
        install_on_lun(drive)
