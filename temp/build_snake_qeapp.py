#!/usr/bin/env python3
"""Build snake.qeapp (type=app) using the same packer the OS ships.

Usage: python temp/build_snake_qeapp.py
Output: temp/snake.qeapp (id=snake, name=Rắn săn mồi)
Payload: out/apps/snake.elf (cross-compiled aarch64-linux-musl static ELF)
Icon: original 32x32 RGB565 drawn here, no copied artwork.
Signing: ECDSA P-256 RFC6979, same builder as build/mk-qeapp.py.
"""
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build"))
import importlib.util
spec = importlib.util.spec_from_file_location(
    "mk_qeapp",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "mk-qeapp.py"),
)
mk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mk)

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ELF = os.path.join(ROOT, "out", "apps", "snake.elf")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "snake.qeapp")

def snake_icon():
    """Original icon: green snake over dark forest grid, red apple.
    No copied artwork; drawn pixel by pixel in this file."""
    W, H = 32, 32
    px = [[(0, 0, 0) for _ in range(W)] for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if (x % 4 == 0) or (y % 4 == 0):
                px[y][x] = (17, 51, 34)
            else:
                px[y][x] = (10, 46, 26)
    # body
    for x in range(5, 18):
        for y in range(18, 21):
            px[y][x] = (34, 255, 153)
    # head
    for x in range(18, 22):
        for y in range(16, 19):
            px[y][x] = (34, 255, 153)
    # eyes
    px[17][19] = (0, 0, 0)
    px[17][21] = (0, 0, 0)
    # apple
    px[10][24] = (224, 51, 51)
    px[11][23] = (224, 51, 51)
    px[11][25] = (224, 51, 51)
    px[12][24] = (224, 51, 51)
    return mk.pack_icon(px)

def main():
    fields = {
        "id": "snake",
        "name": "Rắn săn mồi",
        "version": "1.0.0",
        "type": "app",
    }
    payload = open(ELF, "rb").read()
    icon = snake_icon()
    blob = mk.build_package(fields, icon, payload)
    open(OUT, "wb").write(blob)
    print(f"wrote {OUT} ({len(blob)} bytes, id=snake)")

if __name__ == "__main__":
    main()
