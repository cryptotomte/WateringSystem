#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cryptotomte
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Deterministically gzip a source tree into a staging dir for the littlefs image.

Feature 010 (PR-10): the ESP-IDF build calls this to compress the adapted
frontend assets (firmware/web/) into <dst>/<relpath>.gz before they are packed
into the littlefs `storage` partition image. Deterministic (mtime=0, fixed
compression level) so the build is reproducible (Constitution III) — no build
timestamp leaks into the artifact. Stdlib only; no third-party deps.
"""

import argparse
import gzip
import os
import sys

COMPRESS_LEVEL = 9

# Documentation / non-served files that must not end up on the device volume.
SKIP_SUFFIXES = (".md",)


def gzip_tree(src: str, dst: str) -> int:
    count = 0
    for root, _dirs, files in os.walk(src):
        for name in files:
            if name.lower().endswith(SKIP_SUFFIXES):
                print(f"skip: {os.path.relpath(os.path.join(root, name), src)}")
                continue
            in_path = os.path.join(root, name)
            rel = os.path.relpath(in_path, src)
            out_path = os.path.join(dst, rel + ".gz")
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            with open(in_path, "rb") as f:
                data = f.read()
            # mtime=0 keeps the output byte-identical across builds.
            with gzip.GzipFile(
                filename="", mode="wb", compresslevel=COMPRESS_LEVEL,
                fileobj=open(out_path, "wb"), mtime=0,
            ) as gz:
                gz.write(data)
            print(f"gzip: {rel} -> {rel}.gz ({os.path.getsize(out_path)} bytes)")
            count += 1
    return count


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", required=True, help="source dir to compress")
    ap.add_argument("--dst", required=True, help="staging dir for the .gz files")
    args = ap.parse_args()
    if not os.path.isdir(args.src):
        print(f"error: source dir not found: {args.src}", file=sys.stderr)
        return 1
    os.makedirs(args.dst, exist_ok=True)
    n = gzip_tree(args.src, args.dst)
    if n == 0:
        # Fail loudly rather than ship an asset-less littlefs image as success.
        print(f"error: no assets compressed from {args.src} "
              f"(empty or misconfigured)", file=sys.stderr)
        return 1
    print(f"gzip_assets: {n} file(s) -> {args.dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
