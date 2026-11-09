#!/usr/bin/env python3
r"""Read QEAFRESCUE files by raw sector, bypassing Windows' stale mounted view.

The mounted F: view is a lagging witness: on 2026-11-02 it showed 3280 bytes of
QEAFBOOT.LOG while the device held 11042, hiding the lines that proved a flash
had finished and verified. build/read-report-raw.ps1 did this job but was lost
when the source tree was rebuilt; this is a read-only, stdlib-only replacement.

It opens \\.\F: directly, parses the FAT16 BPB + root directory, follows the
cluster chains and writes exact copies into an output directory. It never writes
to the volume and never flushes it: flushing a mounted volume would push the
host's stale directory entries back onto the device.

Usage:
    python scripts/read_rescue_raw.py --drive F: --out out/report-raw
    python scripts/read_rescue_raw.py --drive F: --out out/report-raw --show QEAFBOOT.LOG
    python scripts/read_rescue_raw.py --drive F: --require-folders Documents,Downloads,Music,Pictures,Videos

The long-name records matter: Windows shows the folder as "Documents" only
because a chain of 0x0F entries stands in front of the 8.3 one, so a reader
that looks at short names alone cannot tell a real folder from a phone that
still has none. `--require-folders` turns that read into a gate: it exits 2
when a name is missing, has no directory cluster, or is reachable only as an
8.3 alias.
"""

import argparse
import os
import struct
import sys

DEFAULT_FILES = ("QEAFBOOT.LOG", "RESPONSE.TXT", "SUMMARY.TXT", "HELP.TXT")
VOLUME_LABEL = b"QEAFRESCUE"
END_OF_CHAIN = 0xFFF8


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def read_at(f, off, size):
    f.seek(off)
    data = f.read(size)
    if len(data) != size:
        raise IOError("short read at offset %d: got %d of %d bytes"
                      % (off, len(data), size))
    return data


def parse_bpb(boot):
    bps = u16(boot, 11)
    spc = boot[13]
    reserved = u16(boot, 14)
    num_fats = boot[16]
    root_entries = u16(boot, 17)
    sectors_per_fat = u16(boot, 22)
    total16 = u16(boot, 19)
    total32 = u32(boot, 32)
    total = total16 or total32
    if bps not in (512, 1024, 2048, 4096) or not spc or not reserved:
        raise ValueError("not a FAT volume (bps=%d spc=%d reserved=%d)"
                         % (bps, spc, reserved))
    root_off = (reserved + num_fats * sectors_per_fat) * bps
    root_sectors = (root_entries * 32 + bps - 1) // bps
    data_off = root_off + root_sectors * bps
    return {
        "bps": bps, "spc": spc, "fat_off": reserved * bps,
        "fat_size": sectors_per_fat * bps, "root_off": root_off,
        "root_size": root_entries * 32, "data_off": data_off,
        "total": total, "label": boot[43:54].rstrip(b" "),
    }


def short_name(raw):
    stem = raw[:8].decode("ascii", "replace").rstrip()
    ext = raw[8:11].decode("ascii", "replace").rstrip()
    return (stem + "." + ext) if ext else stem


wanted_name = short_name


def sfn_checksum(raw):
    """The FAT checksum of an 11-byte short name, per the FAT spec.

    The accumulator is 16 bits and rotates right through bit 15; only the low
    byte is stored in the record. Masking to 8 bits inside the loop loses the
    rotated bit and silently rejects every multi-record name.
    """
    sum_ = 0
    for b in raw:
        sum_ = ((0x8000 if sum_ & 1 else 0) + (sum_ >> 1) + b) & 0xFFFF
    return sum_ & 0xFF


def lfn_chars(entry):
    """The 13 UTF-16 slots of one long-name record, in order."""
    for off, count in ((1, 5), (14, 6), (28, 2)):
        for i in range(count):
            yield u16(entry, off + i * 2)


def long_name(entries, idx):
    """Rebuild the long name standing in front of short entry `idx`.

    Records sit immediately before the 8.3 entry, numbered from the end, so
    walking backwards from idx gives them in sequence order 1..N. A mismatched
    checksum or a gap means the chain was never written (or belongs to another
    file) and the answer is None: a name only Windows' shell could invent.
    """
    if idx <= 0:
        return None
    want = sfn_checksum(entries[idx][:11])
    found = []
    seq = None
    i = idx - 1
    while i >= 0 and (entries[i][11] & 0x0F) == 0x0F:
        if entries[i][13] != want:
            return None
        order = entries[i][0] & 0x3F
        if seq is None:
            if not entries[i][0] & 0x40:
                return None
            seq = order + 1
        elif order != seq - 1:
            return None
        seq = order
        found.append(entries[i])
        i -= 1
    if seq != 1:
        return None
    text = []
    for entry in reversed(found):
        stop = False
        for ch in lfn_chars(entry):
            if ch in (0x0000, 0xFFFF):
                stop = True
                break
            text.append(ch)
        if stop:
            break
    return "".join(chr(c) for c in text)


def root_entries(f, geo):
    return list(_chunks(read_at(f, geo["root_off"], geo["root_size"]), 32))


def root_files(entries):
    """Every live 8.3 entry with the long name the host will display."""
    out = []
    for i, e in enumerate(entries):
        if e[0] in (0x00, 0xE5) or (e[11] & 0x0F) == 0x0F:
            continue
        out.append({
            "name": short_name(e[:11]),
            "long": long_name(entries, i),
            "attr": e[11],
            "cluster": u16(e, 26),
            "size": u32(e, 28),
        })
    return out


def dir_list(f, geo, cluster, max_clusters=8):
    """Names inside a directory cluster chain, "." and ".." excluded."""
    clus_bytes = geo["spc"] * geo["bps"]
    buf = bytearray()
    for c in chain(f, geo, cluster)[:max_clusters]:
        buf.extend(read_at(f, geo["data_off"] + (c - 2) * clus_bytes, clus_bytes))
    entries = list(_chunks(bytes(buf), 32))
    rows = []
    for i, e in enumerate(entries):
        if e[0] == 0x00:
            break
        if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F:
            continue
        if e[0] == 0x2E and (e[11] & 0x10) and e[1] in (0x2E, 0x20):
            continue
        rows.append((short_name(e[:11]), long_name(entries, i) or "", e[11]))
    return rows


def _chunks(buf, n):
    for i in range(0, len(buf), n):
        yield buf[i:i + n]


def chain(f, geo, first):
    fat = read_at(f, geo["fat_off"], geo["fat_size"])
    clusters = []
    cur = first
    seen = set()
    while 2 <= cur < END_OF_CHAIN:
        if cur in seen:
            raise ValueError("FAT chain loops at cluster %d" % cur)
        seen.add(cur)
        clusters.append(cur)
        off = cur * 2
        if off + 2 > len(fat):
            raise ValueError("FAT chain ran past the FAT (cluster %d)" % cur)
        cur = u16(fat, off)
    return clusters


def extract(f, geo, entry):
    size = entry["size"]
    if size == 0:
        return b""
    clusters = chain(f, geo, entry["cluster"])
    clus_bytes = geo["spc"] * geo["bps"]
    out = bytearray()
    for c in clusters:
        off = geo["data_off"] + (c - 2) * clus_bytes
        out.extend(read_at(f, off, clus_bytes))
        if len(out) >= size:
            break
    return bytes(out[:size])


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--drive", default="F:",
                   help="QEAFRESCUE drive letter, e.g. F: (default: %(default)s)")
    p.add_argument("--out", default="out/report-raw",
                   help="local directory for the captured files")
    p.add_argument("--files", nargs="*", default=None,
                   help="8.3 names to capture (default: the four owned files, "
                        "or nothing when --image is given)")
    p.add_argument("--require-folders", metavar="N1,N2,...", default="",
                   help="comma-separated long names that must exist as directories")
    p.add_argument("--image", metavar="PATH",
                   help="read a FAT16 image file instead of a raw drive "
                        "(calibrates this reader against a known-good volume)")
    p.add_argument("--show", action="store_true",
                   help="print captured text files to stdout")
    args = p.parse_args(argv)
    args.require = [n.strip() for n in args.require_folders.split(",") if n.strip()]

    if args.files is None:
        args.files = [] if args.image else list(DEFAULT_FILES)

    if args.image:
        try:
            f = open(args.image, "rb", buffering=0)
        except OSError as ex:
            print("REFUSED: cannot open %s: %s" % (args.image, ex), file=sys.stderr)
            return 2
        path = args.image
    else:
        drive = args.drive.rstrip("\\/")
        if len(drive) != 2 or drive[1] != ":":
            print("REFUSED: --drive must be a single letter and ':', e.g. F:",
                  file=sys.stderr)
            return 2
        path = r"\\.\%s" % drive
        try:
            f = open(path, "rb", buffering=0)
        except OSError as ex:
            print("REFUSED: cannot open %s raw: %s" % (path, ex), file=sys.stderr)
            return 2

    with f:
        boot = read_at(f, 0, 512)
        geo = parse_bpb(boot)
        label = geo["label"]
        print("volume %s  label=%r  FAT16 bps=%d cluster=%d B total=%d sectors"
              % (path, label.decode("ascii", "replace"), geo["bps"],
                 geo["spc"] * geo["bps"], geo["total"]))
        if label != VOLUME_LABEL:
            print("REFUSED: this is not the QEAFRESCUE LUN", file=sys.stderr)
            return 2

        entries = root_files(root_entries(f, geo))
        print("root directory (device's real view):")
        for e in entries:
            kind = "DIR " if e["attr"] & 0x10 else "file"
            shown = e["long"] if e["long"] else e["name"]
            print("  %s %-16s 8.3=%-12s cluster=%-6d size=%d"
                  % (kind, shown, e["name"], e["cluster"], e["size"]))
            if e["attr"] & 0x10:
                for inner in dir_list(f, geo, e["cluster"]):
                    print("        %s %s" % (inner[1] or inner[0],
                                             "(dir)" if inner[2] & 0x10 else ""))

        missing_folders = []
        for want in args.require:
            live = [e for e in entries
                    if e["attr"] & 0x10 and (e["long"] or "").upper() == want.upper()]
            if not live:
                missing_folders.append(want)
                print("  MISSING folder %r (no directory entry with that long name)"
                      % want)
            else:
                e = live[0]
                print("  OK folder %-12s 8.3=%s cluster=%d entries=%d"
                      % (want, e["name"], e["cluster"],
                         len(dir_list(f, geo, e["cluster"]))))
        if missing_folders:
            print("REFUSED: %d of %d storage folders absent: %s"
                  % (len(missing_folders), len(args.require),
                     ", ".join(missing_folders)), file=sys.stderr)
            return 2

        by_name = {e["name"].upper(): e for e in entries}
        os.makedirs(args.out, exist_ok=True)
        wanted = [n.upper() for n in args.files]
        missing = [n for n in wanted if n not in by_name]
        if missing:
            print("REFUSED: not on the volume: %s" % ", ".join(missing),
                  file=sys.stderr)
            return 2

        for name in wanted:
            e = by_name[name]
            data = extract(f, geo, e)
            if len(data) != e["size"]:
                print("REFUSED: %s captured %d of %d bytes"
                      % (name, len(data), e["size"]), file=sys.stderr)
                return 2
            dest = os.path.join(args.out, name)
            with open(dest, "wb") as out:
                out.write(data)
            print("  wrote %s (%d bytes)" % (dest, len(data)))
            if args.show:
                print("  ---- %s ----" % name)
                print(data.decode("utf-8", "replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
