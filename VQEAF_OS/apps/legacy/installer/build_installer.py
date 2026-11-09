#!/usr/bin/env python3
"""Build the QeafdropOs installer app and install it to the QEAFRESCUE drive."""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
QEAF = os.path.dirname(os.path.dirname(os.path.dirname(ROOT)))
OUT = os.path.join(QEAF, "out")

TARGET = os.path.join(OUT, "VQEAF Installer.qeapp")

def build():
    os.system("cd %s && make clean && make" % ROOT)
    app_dir = os.path.join(OUT, "installer")
    os.makedirs(app_dir, exist_ok=True)
    shutil.copy(os.path.join(ROOT, "VQEAF Installer"), os.path.join(app_dir, "VQEAF Installer"))

    manifest = """VQEAF-QEAPP/1
name=VQEAF Installer
id=com.qeaf.installer
version=1.0.0
category=applications
developer=Qeaf Software
size_kb=8
icon=icon.png
"""
    with open(os.path.join(app_dir, "META-INF.QEA"), "w") as f:
        f.write(manifest)

    with open(os.path.join(app_dir, "icon.png"), "wb") as f:
        # 64x64 PNG placeholder with installer icon
        f.write(bytes([
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        ] + [0x20] * 4092))

    shutil.make_archive(os.path.join(OUT, "VQEAF Installer"), "zip", app_dir)
    os.rename(os.path.join(OUT, "VQEAF Installer.zip"), TARGET)
    print("Built:", TARGET)

def install_on_lun(drive):
    if drive:
        path = os.path.join(drive, "VQEAF Installer.qeapp")
        shutil.copy(TARGET, path)
        print("Installed to", drive, "as", "VQEAF Installer.qeapp")

if __name__ == "__main__":
    build()
    drive = None
    for a in sys.argv[1:]:
        if a.startswith("--drive"):
            drive = a.split("=", 1)[1]
        elif a.startswith("-d"):
            drive = a[2:]
    if drive:
        install_on_lun(drive)
    else:
        print("Usage: python build_installer.py [--drive F:]")
