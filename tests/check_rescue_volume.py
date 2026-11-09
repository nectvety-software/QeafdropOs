#!/usr/bin/env python3
"""Second, independent reader for the QEAFRESCUE volume.

tests/storage_fat16_test.c builds the volume with the device's own fs/fat16.c
and walks it back with a C reader. This file parses the same bytes again with a
different implementation - no header shared with the builder except the on-disk
FAT spec itself - so a builder and a reader that agree by coincidence (same
wrong offset, same wrong checksum rule) get caught here.

It reads what a host would read off the LUN: the boot sector, both FAT copies,
the root directory including long-name records, and the cluster each folder
occupies. It also carries its own negative control: a copy of the volume with
one long-name checksum corrupted must fall back to the 8.3 name instead of
reporting the folder by its long name.

Usage: python3 tests/check_rescue_volume.py out/qeafrescue_meta.img
Exit 0 = the volume really does present the storage folders.
"""

import struct
import sys

STORAGE = ["VQEAF", "UserData", "Install"]
ATTR_LFN = 0x0F
ATTR_DIR = 0x10
ATTR_VOLUME = 0x08
LFN_LAST = 0x40

checks = 0
failed = 0


def ok(cond, label):
    global checks, failed
    checks += 1
    print(("ok    " if cond else "FAIL  ") + label)
    if not cond:
        failed += 1
    return cond


def u16(b, off):
    return struct.unpack_from("<H", b, off)[0]


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def sfn_checksum(name11):
    s = 0
    for ch in name11:
        s = ((0x8000 if s & 1 else 0) + (s >> 1) + ch) & 0xFFFF
    return s & 0xFF


def short83(e):
    """The name an 8.3-only reader sees. A volume label keeps its 11 bytes as
    one field; anything else splits base.ext."""
    if e[11] == ATTR_VOLUME:
        return e[0:11].decode("ascii", "replace").rstrip()
    base = e[0:8].decode("ascii", "replace").rstrip()
    ext = e[8:11].decode("ascii", "replace").rstrip()
    return base + ("." + ext if ext else "")


class Volume:
    def __init__(self, img):
        bs = img[0:512]
        self.bps = u16(bs, 11)
        self.spc = bs[13]
        self.reserved = u16(bs, 14)
        self.num_fats = bs[16]
        self.root_entries = u16(bs, 17)
        self.fat_size = u16(bs, 22)
        self.sectors = u32(bs, 32) or u16(bs, 19)
        self.label = bs[43:54].decode("ascii", "replace").rstrip()
        self.fstype = bs[54:62].decode("ascii", "replace").rstrip()
        self.fat_start = self.reserved
        self.root_start = self.fat_start + self.num_fats * self.fat_size
        self.root_bytes = self.root_entries * 32
        self.data_start = self.root_start + (self.root_bytes + self.bps - 1) // self.bps

    def fat(self, which, cluster):
        off = (self.fat_start + which * self.fat_size) * self.bps + cluster * 2
        return u16(self.img, off)

    def cluster_bytes(self, cluster):
        start = (self.data_start + (cluster - 2) * self.spc) * self.bps
        return self.img[start:start + self.spc * self.bps]

    def root(self):
        """Yields (entry_index, bytes) for every live 8.3 entry, with the long
        name attached when a valid LFN run precedes it."""
        start = self.root_start * self.bps
        out = []
        pending = {}
        for i in range(self.root_entries):
            e = self.img[start + i * 32:start + (i + 1) * 32]
            attr = e[11]
            if attr == 0x00:
                break
            if e[0] == 0xE5:
                continue
            if attr == ATTR_LFN:
                seq = e[0]
                if seq & LFN_LAST:
                    pending = {"n": seq & 0x3F, "sum": e[13], "parts": {}}
                if not pending:
                    continue
                if (seq & 0x3F) != pending["n"]:
                    pending = {}
                    continue
                pending["parts"][pending["n"]] = e
                pending["n"] -= 1
                continue
            long_name = None
            if pending and pending["n"] == 0 and pending["sum"] == sfn_checksum(e[0:11]):
                chars = []
                stop = False
                for k in sorted(pending["parts"]):
                    p = pending["parts"][k]
                    for off, cnt in ((1, 5), (14, 6), (28, 2)):
                        for j in range(cnt):
                            unit = u16(p, off + j * 2)
                            if unit == 0x0000:
                                stop = True
                                break
                            if unit != 0xFFFF:
                                chars.append(chr(unit))
                        if stop:
                            break
                    if stop:
                        break
                long_name = "".join(chars)
            out.append((i, e, short83(e), long_name, attr, u16(e, 26)))
            pending = {}
        return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "out/qeafrescue_meta.img"
    with open(path, "rb") as f:
        raw = f.read()
    print("== rescue volume: independent read of %s (%d bytes) ==" % (path, len(raw)))

    img = bytearray(raw)
    v = Volume(img)
    v.img = img

    ok(img[510] == 0x55 and img[511] == 0xAA, "boot sector signature")
    ok(v.bps == 512, "512 bytes per sector")
    ok(v.spc == 8, "8 sectors per cluster (4 KiB)")
    ok(v.num_fats == 2, "two FAT copies")
    ok(v.root_entries == 512, "a 512-entry root directory")
    ok(v.sectors * v.bps == 32 * 1024 * 1024, "the BPB sizes the volume at 32 MiB")
    ok(v.fstype == "FAT16", "the type string is FAT16")
    ok(v.label == "QEAFRESCUE", "the BPB label is QEAFRESCUE")
    print("      layout: FAT %d, root %d, data %d" % (v.fat_start, v.root_start, v.data_start))

    ok(v.fat(0, 0) == 0xFFF8 and v.fat(1, 0) == 0xFFF8,
       "FAT[0] media descriptor, mirrored in both FATs")
    ok(v.fat(0, 1) == 0xFFFF and v.fat(1, 1) == 0xFFFF,
       "FAT[1] end-of-chain, mirrored in both FATs")

    entries = v.root()
    for i, e, s83, long_name, attr, cluster in entries:
        print("      root[%d] %-11s attr 0x%02X cluster %-4d %s" %
              (i, long_name or s83, attr, cluster,
               "(long name + %s)" % s83 if long_name else "(8.3 only)"))

    labels = [x for x in entries if x[4] == ATTR_VOLUME]
    dirs = [x for x in entries if x[4] & ATTR_DIR]
    ok(len(labels) == 1 and entries[0][4] == ATTR_VOLUME,
       "one volume label entry, first in the root")
    ok(labels and labels[0][2] == "QEAFRESCUE", "the label reads back as QEAFRESCUE")

    names = [x[3] for x in dirs]
    for want in STORAGE:
        ok(want in names, "folder '%s' is present under its exact name" % want)
    ok(len(dirs) == len(STORAGE), "and there are exactly %d folders" % len(STORAGE))
    ok(all(x[3] for x in dirs), "every folder carries a long name, not just an 8.3 guess")
    ok(len({x[2] for x in dirs}) == len(dirs), "the short names are all distinct")

    for i, e, s83, long_name, attr, cluster in dirs:
        good = (cluster >= 2 and v.fat(0, cluster) == 0xFFFF and
                v.fat(1, cluster) == 0xFFFF)
        ok(good, "'%s' cluster %d is allocated and closed in both FATs" % (long_name, cluster))
        d = v.cluster_bytes(cluster)
        ok(d[0:1] == b"." and d[11] == ATTR_DIR and u16(d, 26) == cluster and
           d[32:34] == b".." and d[43] == ATTR_DIR and u16(d, 32 + 26) == 0,
           "'%s' opens with '.' (itself) and '..' (the root)" % long_name)

    # ---- negative control: corrupt one long-name checksum -------------------
    # The reader must drop that one long name and fall back to the 8.3 entry.
    # Without this, "every folder carries a long name" could be the decoder
    # inventing names the volume never wrote.
    k = v.root_start * v.bps
    victim = None
    for i in range(v.root_entries):
        p = k + i * 32
        if img[p + 11] == ATTR_LFN:
            img[p + 13] ^= 0xFF
            victim = short83(img[p + 32:p + 64])
            break
    corrupted = Volume(bytearray(img))
    corrupted.img = img
    again = corrupted.root()
    still_dirs = [x for x in again if x[4] & ATTR_DIR]
    lost = [x for x in still_dirs if x[3] is None]
    ok(victim is not None, "the control found a long-name record to corrupt")
    ok(len(lost) == 1, "exactly one folder loses its long name - the decoder reads the checksum")
    ok(lost and lost[0][2] == victim, "and it is the folder whose record was tampered with")
    ok(len(still_dirs) == len(dirs), "the folder itself is still there, only its name falls back to 8.3")
    ok(sum(1 for x in still_dirs if x[3]) == len(dirs) - 1,
       "the remaining %d keep their long names" % (len(dirs) - 1))

    # Second control: the same walk over a root that holds nothing. Without it
    # the folder loop could be reading the names out of the source, not bytes.
    blank = bytearray(raw)
    root_base = v.root_start * v.bps
    blank[root_base:root_base + v.root_entries * 32] = bytes(v.root_entries * 32)
    bv = Volume(blank)
    bv.img = blank
    ok(bv.root() == [], "the same walk over a zeroed root finds no entries at all")
    ok(len([x for x in bv.root() if x[4] & ATTR_DIR]) == 0, "and no folders")

    print("\n%d checks, %d failed" % (checks, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
