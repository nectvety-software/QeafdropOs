#!/usr/bin/env python3
"""newc cpio writer, used as a stand-in for 'cpio -o -H newc'.

The initramfs is the one artefact this project cannot fake: a single wrong
padding byte makes the kernel's unpacker stop early and the boot dies with
'No working init found'. It lives here rather than shelling out because Git
Bash on Windows ships no cpio at all.

Three details of newc are easy to get wrong and impossible to see in a hex
dump, so they are stated once, here, and --check enforces them:

  * the header is 110 bytes - magic plus thirteen 8-digit hex fields, and
    because 110 % 4 == 2 the name block is usually already aligned; padding
    counts from the start of the record, not from the start of the name;
  * the mode field keeps its file type bits (0o040000 directory, 0o120000
    symlink, 0o100000 regular). A bare permission mask packs a tree of
    unreadable non-entries;
  * a symlink's payload is its target and filesize counts it, because the
    kernel's unpacker advances the cursor by filesize for every entry type.

Symlinks that Git Bash on Windows could not create are read from the file in
$MK_CPIO_LINKS (default: out/stage-symlinks.txt), one 'path<TAB>target' per
line; scripts/mk_initramfs.py sets it for you.

usage: python scripts/mk_initramfs.py            (stage -> cpio + gzip)
       python scripts/mk_cpio.py --check out/qeaf-initramfs.cpio
"""
import os
import re
import stat
import sys

HEADER = 110


def header(mode, filesize, namesize):
    fields = [
        "070701",
        "%08X" % 1,                    # ino, all equal - nothing deduplicates
        "%08X" % mode,
        "%08X" % 0,                    # uid
        "%08X" % 0,                    # gid
        "%08X" % (2 if stat.S_ISDIR(mode) else 1),
        "%08X" % 0,                    # mtime: 0 keeps two builds identical
        "%08X" % filesize,
        "%08X" % 0,                    # devmajor
        "%08X" % 0,                    # devminor
        "%08X" % 0,                    # rdevmajor
        "%08X" % 0,                    # rdevminor
        "%08X" % namesize,
        "%08X" % 0,                    # check, unused for newc
    ]
    head = "".join(fields).encode("ascii")
    if len(head) != HEADER:
        raise AssertionError("newc header must be 110 bytes, built %d" % len(head))
    return head


def fill(block, base):
    """NUL padding until (base + len(block)) is a multiple of 4."""
    slack = -(base + len(block)) % 4
    return block + b"\0" * slack


def mode_of(name, st):
    """Git Bash cannot carry an exec bit on a file without a .exe suffix, so a
    tree staged on Windows would otherwise pack /init as 0644 and the kernel
    would answer 'No working init found'. Everything under bin/ or sbin/ is a
    program in this initramfs, so record it as one; on Linux nothing changes."""
    mode = st.st_mode
    if stat.S_ISREG(mode) and (re.match(r"^\./?(bin|sbin)/", name) or name in ("init", "./init")):
        mode |= 0o111
    if stat.S_ISLNK(mode):
        mode = 0o120000
    return mode


def emit(out, name, mode, payload):
    nameblk = name.encode("utf-8") + b"\0"
    out.write(header(mode, len(payload), len(nameblk)))
    # fill() aligns relative to the start of what follows, so after the name
    # block the record is a multiple of 4 bytes long and the payload only has
    # to be padded on its own length.
    out.write(fill(nameblk, HEADER))
    if payload:
        out.write(fill(payload, 0))


def walk(names):
    """Yield (name, mode, payload) in unpack order: parents before children,
    or the unpacker mkdirs into a tree that is not there yet."""
    for name in sorted(names, key=lambda n: (n.count("/"), n)):
        st = os.lstat(name)
        mode = mode_of(name, st)
        if stat.S_ISLNK(st.st_mode):
            yield name, mode, os.readlink(name).encode("utf-8")
        elif stat.S_ISDIR(st.st_mode):
            yield name, mode, b""
        elif stat.S_ISREG(st.st_mode):
            with open(name, "rb") as f:
                yield name, mode, f.read()
        else:
            sys.stderr.write("mk_cpio: skipping special file %s\n" % name)


def links_from_env():
    """Applet links the stage script had to record instead of creating, because
    Git Bash without Developer Mode turns `ln -s` into a full copy. One line
    per link, path-inside-stage<TAB>target; packed as cpio symlink entries,
    which is what a Linux host's real symlinks produce."""
    path = os.environ.get("MK_CPIO_LINKS")
    if not path or not os.path.exists(path):
        return []
    out = []
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        name, _, target = line.partition("\t")
        if name and target:
            out.append((name if name.startswith("./") else "./" + name,
                        target))
    return out


def write(stdin_names, out, links=()):

    n = 0
    for name, mode, payload in walk([l.rstrip("\n") for l in stdin_names
                                     if l.strip("\n")]):
        emit(out, name, mode, payload)
        n += 1
    for name, target in links:
        emit(out, name, 0o120000 | 0o777, target.encode("utf-8"))
        n += 1
    emit(out, "TRAILER!!!", 0o100000, b"")
    return n


def read(path):
    """Parse an archive the way the kernel's unpacker does. Returns a list of
    (name, mode, filesize) or raises ValueError with the offset that broke."""
    d = open(path, "rb").read()
    off, entries = 0, []
    while True:
        if off + HEADER > len(d):
            raise ValueError("ran off the end at %#x" % off)
        head = d[off:off + HEADER]
        if head[:6] != b"070701":
            raise ValueError("magic lost at %#x" % off)
        f = [int(head[6 + i * 8:14 + i * 8], 16) for i in range(13)]
        mode, filesize, namesize = f[1], f[6], f[11]
        end = off + HEADER + namesize
        if end > len(d):
            raise ValueError("name runs past the end at %#x" % off)
        name = d[off + HEADER:end - 1].decode("utf-8", "replace")
        # Align the record offset after the name, not the name length: 110 is
        # not a multiple of 4, so padding namesize alone would land one short.
        data = (off + HEADER + namesize + 3) & ~3
        entries.append((name, mode, filesize))
        if name == "TRAILER!!!":
            if data + filesize != len(d):
                raise ValueError("%d bytes past the trailer (%#x vs %#x)"
                                 % (len(d) - data, data, len(d)))
            return entries
        if not 0o40000 <= mode <= 0o170000:
            raise ValueError("bad mode %#o for %s at %#x" % (mode, name, off))
        off = data + ((filesize + 3) & ~3)


def check(path):
    try:
        entries = read(path)
    except ValueError as e:
        sys.stderr.write("mk_cpio: %s: %s\n" % (path, e))
        return 1
    total = sum(s for _, m, s in entries if not stat.S_ISLNK(m))
    for name, mode, _ in entries:
        if stat.S_ISREG(mode) and not mode & 0o111:
            base = name.split("/")[-1]
            if base in ("init", "qeafinit", "busybox", "sh", "qeadm", "qeafkeyd"):
                sys.stderr.write("mk_cpio: %s: %s is not executable, "
                                 "the kernel cannot run it\n" % (path, name))
                return 1
    print("mk_cpio: %s ok, %d entries, %d bytes of payload, %d bytes packed"
          % (path, len(entries) - 1, total, os.path.getsize(path)))
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        if len(sys.argv) != 3:
            sys.stderr.write("usage: mk_cpio.py --check FILE\n")
            return 2
        return check(sys.argv[2])
    args = sys.argv[1:]
    root = None
    if args[:1] == ["--root"]:
        if len(args) != 2:
            sys.stderr.write("usage: mk_cpio.py --root DIR < list\n")
            return 2
        root = args[1]
    elif args:
        sys.stderr.write("usage: find . | mk_cpio.py > out.cpio\n"
                         "       mk_cpio.py --root DIR < list\n"
                         "       mk_cpio.py --check out.cpio\n")
        return 2
    if root is not None:
        # The list is relative to the tree, so the process has to be inside it;
        # callers that pipe 'find' from a subshell would otherwise lstat against
        # the wrong directory.
        os.chdir(root)
    links = links_from_env()
    if links:
        sys.stderr.write("mk_cpio: %d links from %s\n"
                         % (len(links), os.environ.get("MK_CPIO_LINKS")))
    n = write(sys.stdin, sys.stdout.buffer, links)
    sys.stderr.write("mk_cpio: %d entries\n" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
