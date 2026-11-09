#!/usr/bin/env python3
"""Import pinned MIT WinUI 2.3 originals for local porting, not direct compilation.

This tool REQUIRES explicit user invocation and network access. The checked-in
adapted Fluent resources stay independent of this reference import. Keep upstream
copyright and license intact. Do not include proprietary Windows SDK binaries.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import urllib.request
import zipfile

TAG = "v2.3.191129002"
COMMIT = "2237b09d9ba6052414ddccf3d61316de20564303"
URL = f"https://codeload.github.com/microsoft/microsoft-ui-xaml/zip/{COMMIT}"
# WinUI 2.3 controls: canonical definitions, template and theme resources,
# supporting C++ source/IDL; runtime behavior requires a Velora port.
CONTROL_PATHS = (
    "dev/CommonStyles/",
    "dev/NumberBox/",
    "dev/NavigationView/",
    "dev/TabView/",
    "dev/TreeView/",
    "dev/Repeater/",
    "dev/RadioButtons/",
    "dev/ToggleSwitch/",
    "dev/ProgressBar/",
    "dev/ContentDialog/",
    "dev/TeachingTip/",
    "dev/RatingControl/",
    "dev/PersonPicture/",
    "dev/AutoSuggestBox/",
    "dev/ColorPicker/",
    "dev/SwipeControl/",
    "dev/SplitButton/",
    "dev/DropDownButton/",
    "dev/TwoPaneView/",
)
EXTENSIONS = {".cpp", ".h", ".hpp", ".idl", ".xaml", ".resw"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, help="Local copy of the pinned upstream ZIP")
    parser.add_argument("--output", type=Path, default=Path("build/reference/winui2.3/imported"))
    args = parser.parse_args()
    if args.archive:
        data = args.archive.read_bytes()
    else:
        with urllib.request.urlopen(URL, timeout=90) as response:
            data = response.read()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    hashes = {}
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = z.namelist()
        prefix = names[0].split("/", 1)[0] + "/"
        if f"{prefix}LICENSE" not in names:
            raise ValueError("Upstream LICENSE not found")
        license_bytes = z.read(prefix + "LICENSE")
        if b"MIT License" not in license_bytes:
            raise ValueError("Unexpected license")
        (output / "LICENSE").write_bytes(license_bytes)
        for entry in z.infolist():
            if entry.is_dir() or not entry.filename.startswith(prefix):
                continue
            relative = entry.filename[len(prefix):]
            posix = PurePosixPath(relative)
            if ".." in posix.parts or posix.is_absolute():
                raise ValueError("Unsafe ZIP path")
            if not any(relative.startswith(p) for p in CONTROL_PATHS) or posix.suffix.lower() not in EXTENSIONS:
                continue
            target = output.joinpath(*posix.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            blob = z.read(entry)
            target.write_bytes(blob)
            hashes[relative] = hashlib.sha256(blob).hexdigest()
    if not hashes:
        raise ValueError("No matching WinUI source files were imported")
    (output / "UPSTREAM.json").write_text(json.dumps({
        "repository":"microsoft/microsoft-ui-xaml", "tag": TAG, "commit": COMMIT,
        "source": URL, "archive_sha256": hashlib.sha256(data).hexdigest(),
        "files": hashes
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Imported {len(hashes)} pinned MIT source files under {output}")


if __name__ == "__main__":
    main()
