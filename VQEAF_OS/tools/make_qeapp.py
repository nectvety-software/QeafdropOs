#!/usr/bin/env python3
"""Build a .qeapp package for QeafdropOs.

A .qeapp is a ZIP (stored, no compression needed) with:

    manifest.json   required: id, name, version, entry, min_os, kind
    src/............C source + headers (entry declares `lv_obj_t *create(lv_obj_t *parent)`)
    assets/.........icon.png (64x64), labels/*.png optional
    README.md

Install path (follows QEAFRESCUE filesystem):
    host  F:\\Apps\\<id>.qeapp  ->  guest  /Apps/<id>.qeapp  (FAT16 LUN)

The OS lists .qeapp in Files > Apps and validates the manifest (JSON fields +
ZIP entries). Running new native code needs a rebuild until the dlopen ABI
lands; this step makes the package + folder contract testable now.
"""
import argparse
import json
import os
import sys
import zipfile

REQUIRED = ("id", "name", "version", "entry", "min_os", "kind")


def validate_manifest(data):
    missing = [k for k in REQUIRED if k not in data]
    if missing:
        return f"manifest missing: {missing}"
    if not isinstance(data["id"], str) or not data["id"]:
        return "id must be a non-empty string"
    if data["kind"] not in ("app", "theme", "data"):
        return f"kind must be app|theme|data, got {data.get('kind')!r}"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source", help="app directory with manifest.json + src/ + assets/")
    ap.add_argument("output", help="output .qeapp path")
    args = ap.parse_args()

    manifest_path = os.path.join(args.source, "manifest.json")
    if not os.path.isfile(manifest_path):
        raise SystemExit(f"missing {manifest_path}")
    with open(manifest_path, encoding="utf-8-sig") as fh:
        manifest = json.load(fh)
    err = validate_manifest(manifest)
    if err:
        raise SystemExit(err)

    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(manifest_path, "manifest.json")
        for root, _dirs, files in os.walk(args.source):
            rel = os.path.relpath(root, args.source)
            if rel == ".":
                continue
            for name in files:
                full = os.path.join(root, name)
                arc = os.path.join(rel, name).replace(os.sep, "/")
                z.write(full, arc)
    size = os.path.getsize(args.output)
    print(f"wrote {args.output} ({size} bytes, id={manifest['id']})")


if __name__ == "__main__":
    main()
