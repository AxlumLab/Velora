#!/usr/bin/env python3
"""Simple file-naming and module-layout guard; formatting is checked with clang-format separately."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent
issues = []
for file in (root / "modules").rglob("*"):
    if not file.is_file() or file.suffix not in {".cpp", ".hpp"}:
        continue
    if file.name == "globals.hpp":
        continue
    if not re.fullmatch(r"[A-Z][A-Za-z0-9]*\.(cpp|hpp)", file.name):
        issues.append(f"non-PascalCase source: {file.relative_to(root)}")
for invalid in ["modules/text", "modules/graphics/src/velora/text/CMakeLists.txt"]:
    if (root / invalid).exists():
        issues.append(f"separate text module still present: {invalid}")
for issue in issues:
    print(issue)
print(f"Velora style check: {'FAIL' if issues else 'PASS'} ({len(issues)} issue(s))")
sys.exit(bool(issues))
