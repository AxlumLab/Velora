#!/usr/bin/env python3
"""Format Velora's handwritten C++ with the repository .clang-format.

Covers modules/*/src, tests/*.cpp and examples. Never touches the RkGen reference parser
(modules/markup/rkgen, except our adapter legacy_frontend.cpp), parser/compiler test inputs
under tests/<suite>/, or generated code. Pinned: clang-format 19.1.5 (Visual Studio 2022 LLVM).

    python scripts/format_sources.py --check        # CI: fail when a file is not formatted
    python scripts/format_sources.py --write [paths]
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

PINNED = "19.1.5"
ROOT = pathlib.Path(__file__).resolve().parents[1]


def sources(paths):
    if paths:
        return [ROOT / p for p in paths]
    out = list((ROOT / "modules").glob("*/src/**/*.[ch]pp"))
    out.append(ROOT / "modules/markup/rkgen/legacy/parser/legacy_frontend.cpp")
    out += (ROOT / "tests").glob("*.cpp")
    out += (ROOT / "examples").glob("**/*.[ch]pp")
    return sorted(set(out))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    parser.add_argument("paths", nargs="*", help="files relative to the repository root")
    args = parser.parse_args()

    clang_format = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if clang_format is None:
        print("clang-format not found; set CLANG_FORMAT", file=sys.stderr)
        return 2
    version = subprocess.run([clang_format, "--version"], capture_output=True, text=True).stdout
    if PINNED not in version:
        print(f"warning: expected clang-format {PINNED}, found: {version.strip()}", file=sys.stderr)

    files = sources(args.paths)
    flags = ["--dry-run", "--Werror"] if args.check else ["-i"]
    result = subprocess.run([clang_format, "--style=file", *flags, *map(str, files)], check=False)
    print(f"processed {len(files)} source files")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
