#!/usr/bin/env python3
"""Reject retired Velora object wrappers, without banning native Windows COM APIs."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent
retired = (
    "cmake/VeloraAbi.cmake",
    "modules/foundation/src/velora/object",
    "modules/markup/grammar/abi",
    "modules/markup/src/velora/tools/RkGenAbi.cpp",
    "modules/markup/src/velora/reflect/AbiCodegen.hpp",
    "modules/markup/src/velora/reflect/AbiCodegen.cpp",
    "modules/runtime/src/velora/runtime/object/RuntimeContext.hpp",
    "tests/abi2",
)
issues = [f"retired path restored: {name}" for name in retired if (root / name).exists()]
pattern = re.compile(
    r"velora/object/|control_projections\.hpp|VELORA_ABI_|rkgen_abi|"
    r"\b(?:InterfaceRef|AbiResult|WeakRefControl|BridgeLifetime)\b"
)
extensions = {".hpp", ".cpp", ".cmake", ".rkmeta"}
files = [root / "CMakeLists.txt"]
for area in ("modules", "cmake", "tests", "examples"):
    files.extend(path for path in (root / area).rglob("*")
                 if path.is_file() and (path.suffix in extensions or path.name == "CMakeLists.txt"))
for path in files:
    for line_number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if pattern.search(line):
            issues.append(f"{path.relative_to(root)}:{line_number}: retired object API reference")
for issue in issues:
    print(issue)
print(f"Velora workspace check: {'FAIL' if issues else 'PASS'} ({len(issues)} issue(s))")
sys.exit(bool(issues))
