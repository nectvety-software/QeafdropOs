#!/usr/bin/env python3
"""Pack out/stage into the initramfs the violet kernel unpacks as its rootfs.

Two artefacts, one command:

    out/qeaf-initramfs.cpio       newc archive (validated by scripts/mk_cpio.py)
    out/qeaf-initramfs.cpio.gz    gzip member, mtime 0 so two builds are identical

The kernel on violet has CONFIG_RD_GZIP (its own Image is gzipped) and
`# CONFIG_RD_LZ4 is not set`, so gzip is the only compression that can boot.
The symlinks Git Bash could not create are taken from
out/stage-symlinks.txt; without them /init and the busybox applets are
missing and the boot dies with 'No working init found'.

usage: python scripts/mk_initramfs.py
       python scripts/mk_initramfs.py --stage out/stage --out out
"""
import argparse
import gzip
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mk_cpio  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", default="out/stage")
    ap.add_argument("--links", default="out/stage-symlinks.txt")
    ap.add_argument("--out", default="out")
    args = ap.parse_args(argv)

    root = os.path.abspath(args.stage)
    if not os.path.isdir(root):
        print("mk_initramfs: %s missing" % args.stage, file=sys.stderr)
        return 1
    links_path = os.path.abspath(args.links) if args.links else ""
    if links_path and os.path.isfile(links_path):
        os.environ["MK_CPIO_LINKS"] = links_path
    else:
        print("mk_initramfs: %s missing - symlinks will not be packed"
              % args.links, file=sys.stderr)

    outdir = os.path.abspath(args.out)
    os.makedirs(outdir, exist_ok=True)
    cpio_path = os.path.join(outdir, "qeaf-initramfs.cpio")
    gz_path = os.path.join(outdir, "qeaf-initramfs.cpio.gz")

    here = os.getcwd()
    try:
        os.chdir(root)
        names = []
        for dirpath, dirs, files in os.walk("."):
            for name in dirs + files:
                rel = os.path.relpath(os.path.join(dirpath, name), ".").replace(os.sep, "/")
                names.append("./" + rel)
        links = mk_cpio.links_from_env()
        if links:
            print("mk_initramfs: %d symlinks from %s"
                  % (len(links), os.path.basename(links_path)), file=sys.stderr)
        with open(cpio_path, "wb") as f:
            count = mk_cpio.write(names, f, links)
    finally:
        os.chdir(here)

    if mk_cpio.check(cpio_path) != 0:
        return 1

    data = open(cpio_path, "rb").read()
    with open(gz_path, "wb") as f:
        f.write(gzip.compress(data, 9, mtime=0))
    print("mk_initramfs: %d entries, %d bytes cpio, %d bytes gzip -> %s"
          % (count, len(data), os.path.getsize(gz_path), gz_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
