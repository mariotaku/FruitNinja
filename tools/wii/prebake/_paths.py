#!/usr/bin/env python3
"""Shared repo-root / output-dir resolution for the Wii font prebake generators.

Every generator in this directory reads the shipped asset dump
(FruitNinjaBada/Data/{stringtables,xml}) and writes its products into one
output directory. Both locations are resolved the same way:

    --root <dir>   repo root. Default: the nearest ancestor of this file that
                   holds both CMakeLists.txt and tools/wii. Walking up by
                   marker keeps the scripts working wherever they are moved to,
                   instead of hardcoding a "../.." hop count.
    --out <dir>    output directory (created if missing). Default:
                   <root>/tmp/prebake, which is where a dev running these by
                   hand expects them, and what the older in-tmp copies used.

The CMake Wii build passes an explicit --out of <build>/prebake so a fresh
checkout (CI) regenerates the plan instead of depending on gitignored tmp/.
"""

import argparse
import os


def find_repo_root(start):
    d = os.path.abspath(start)
    while True:
        if (os.path.isfile(os.path.join(d, "CMakeLists.txt"))
                and os.path.isdir(os.path.join(d, "tools", "wii"))):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            raise SystemExit(
                "prebake: no repo root (CMakeLists.txt + tools/wii) above " + start)
        d = parent


def parse_args(description):
    """Parse the shared --root/--out options. Returns (root, out_dir)."""
    ap = argparse.ArgumentParser(description=description)
    ap.add_argument("--root", default=None,
                    help="repo root (default: auto-detected from this script's path)")
    ap.add_argument("--out", default=None,
                    help="output directory (default: <root>/tmp/prebake)")
    args = ap.parse_args()

    root = (os.path.abspath(args.root) if args.root
            else find_repo_root(os.path.dirname(os.path.abspath(__file__))))
    out = (os.path.abspath(args.out) if args.out
           else os.path.join(root, "tmp", "prebake"))
    os.makedirs(out, exist_ok=True)
    return root, out


def stringtables_dir(root):
    return os.path.join(root, "FruitNinjaBada", "Data", "stringtables")


def xml_dir(root):
    return os.path.join(root, "FruitNinjaBada", "Data", "xml")
