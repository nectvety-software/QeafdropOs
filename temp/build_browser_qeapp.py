#!/usr/bin/env python3
"""Tạo gói trình duyệt web .qeapp (type=web) cho VQEAF OS.

Định dạng QEAPP/2 (xem build/mk-qeapp.py):
  offset  size  nội dung
  0       8     ASCII "QEAPP2\r\n"
  8       4     độ dài manifest (little-endian)
  12      4     độ dài icon (0 hoặc 2048)
  16      4     độ dài payload (0 cho type=web)
  20      32    SHA-256 manifest
  52      32    SHA-256 icon (hoặc SHA-256(rỗng))
  84      32    SHA-256 payload (hoặc SHA-256(rỗng))
  116     -     manifest bytes
  -       76    "QSIGP256" | key-id u32 LE | r 32 BE | s 32 BE

Manifest (key=value, mỗi dòng một cặp):
  id=...          (a-z0-9_-)
  name=...        (UTF-8, hiển thị trên kính)
  version=...
  type=web
  entry=https://... (URL trang chủ)
"""

import sys
import os
import struct
import hashlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build"))
import importlib.util
spec = importlib.util.spec_from_file_location(
    "mk_qeapp",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "mk-qeapp.py"),
)
mk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mk)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "browser.qeapp")

def browser_icon():
    """Icon trình duyệt: hình cầu 땅 xanh lá cây trên nền tối. Vẽ thủ công, không copy ảnh."""
    W, H = 32, 32
    px = [[(0x00, 0x0A, 0x10) for _ in range(W)] for _ in range(H)]
    # Trái đất xanh lá (vòng tròn giữa)
    cx, cy, r = 16, 16, 10
    for y in range(H):
        for x in range(W):
            dx = x - cx
            dy = y - cy
            if dx*dx + dy*dy <= r*r:
                px[y][x] = (0x00, 0xD2, 0xD2)  # cá s biển
            elif dx*dx + dy*dy <= (r+2)*(r+2):
                px[y][x] = (0x00, 0x8B, 0x9B)  # viền
    # Đường appropriately: một đường ngang và một đường dọc qua tâm
    for x in range(cx - 2, cx + 3):
        px[cy][x] = (0xFF, 0xFF, 0xFF)
    for y in range(cy - 2, cy + 3):
        px[y][cx] = (0xFF, 0xFF, 0xFF)
    return mk.pack_icon(px)

def main():
    fields = {
        "id": "browser",
        "name": "Trình duyệt Web",
        "version": "1.0.0",
        "type": "web",
        "entry": "https://vqeaf.dev/",
    }
    payload = b""  # type=web không có payload ELF
    icon = browser_icon()
    blob = mk.build_package(fields, icon, payload)
    open(OUT, "wb").write(blob)
    print("wrote %s (%d bytes, id=browser, type=web)" % (OUT, len(blob)))

if __name__ == "__main__":
    main()
