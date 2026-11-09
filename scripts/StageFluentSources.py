#!/usr/bin/env python3
"""Stage pinned control sources for destructive porting into Fluent; never compile WinRT.

Reads the verified local reference produced by the original source import. No network,
upstream build projects, generated projections, tests or activation runtime are imported.
Use --check to verify the checked-in staging area without the original reference cache.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
DESTINATION = ROOT / "modules/fluent/porting"
COMMIT = "2237b09d9ba6052414ddccf3d61316de20564303"
AREAS = (
    "AnimatedVisualPlayer", "AutoSuggestBox", "CalendarDatePicker", "CalendarView",
    "CheckBox", "Collections", "ColorPicker", "ComboBox", "CommandBarFlyout",
    "Common", "CommonStyles", "ContentDialog", "DatePicker", "DropDownButton",
    "Effects", "FlipView", "IconSource", "inc", "Interactions", "LayoutPanel",
    "Lights", "Materials", "MenuBar", "MenuFlyout", "NavigationView", "NumberBox",
    "ParallaxView", "PersonPicture", "Pivot", "ProgressBar", "PullToRefresh",
    "RadioButtons", "RadioMenuFlyoutItem", "RatingControl", "Repeater", "ResourceHelper",
    "ScrollBar", "Scroller", "ScrollViewer", "Slider", "SplitButton", "SplitView",
    "SwipeControl", "TabView", "TeachingTip", "TimePicker", "ToolTip", "TreeView",
    "TwoPaneView",
)
TEXT = {".cpp", ".h", ".hpp", ".idl", ".xaml", ".resw", ".hlsl", ".hlsli", ".fx"}
EXTENSIONS = TEXT | {".png", ".jpg", ".jpeg", ".svg"}
MANIFEST = "Sources.json"


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def normalized(blob, extension):
    if extension not in TEXT:
        return blob
    # Retain copyright/comments and program text; normalize imported source layout only.
    text = blob.decode("utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")
    if extension == ".xaml":
        text = re.sub(r"(?m)^[ \t]+", lambda m: m[0].expandtabs(4) if " \t" in m[0] else m[0], text)
    return ("\n".join(line.rstrip(" \t") for line in text.split("\n")).rstrip("\n") + "\n").encode("utf-8")


def checked_target(relative):
    target = (DESTINATION / relative).resolve()
    if not target.is_relative_to(DESTINATION.resolve()) or target == DESTINATION.resolve():
        raise ValueError(f"Unsafe staging path: {relative}")
    return target


def check():
    manifest = json.loads((DESTINATION / MANIFEST).read_text(encoding="utf-8"))
    if manifest["commit"] != COMMIT:
        raise ValueError("Unexpected upstream revision")
    expected = {MANIFEST}
    for entry in manifest["files"]:
        target = checked_target(entry["target"])
        if digest(target.read_bytes()) != entry["staged_sha256"]:
            raise ValueError(f"Staged source changed: {entry['target']}; move adapted code to src/resources")
        expected.add(entry["target"])
    actual = {p.relative_to(DESTINATION).as_posix() for p in DESTINATION.rglob("*") if p.is_file()}
    if actual != expected:
        raise ValueError(f"Staging file set differs: {sorted(actual ^ expected)}")
    license_blob = (ROOT / "modules/fluent/LICENSE-WinUI2.3.txt").read_bytes()
    if digest(license_blob) != manifest["license_sha256"]:
        raise ValueError("WinUI MIT license differs")
    print(f"Fluent staged sources: PASS ({len(manifest['files'])} files, {len(AREAS)} areas; not runtime registrations)")


def stage(reference):
    provenance = json.loads((reference / "UPSTREAM.json").read_text(encoding="utf-8"))
    if provenance["commit"] != COMMIT or provenance["tag"] != "v2.3.191129002":
        raise ValueError("Reference is not the pinned WinUI 2.3 revision")
    original_hashes = {entry["path"]: entry["sha256"] for entry in provenance["files"]}
    source_root = reference / "upstream"
    roots = [(name, f"dev/{name}") for name in AREAS]
    roots += [("Contracts", "idl"), ("DensityStyles", "dev/dll/DensityStyles")]
    entries, blobs, areas = [], {}, []
    for area, relative in roots:
        folder = source_root / relative
        if not folder.is_dir():
            raise ValueError(f"Missing source area: {relative}")
        area_entries = []
        for path in sorted(folder.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in EXTENSIONS:
                continue
            local = path.relative_to(folder)
            if any("test" in part.lower() for part in local.parts[:-1]):
                continue
            source = path.relative_to(source_root).as_posix()
            original = path.read_bytes()
            if digest(original) != original_hashes.get(source):
                raise ValueError(f"Original hash mismatch: {source}")
            target = f"{area}/{local.as_posix()}"
            # The only dotted C++ basename in this revision; preserve project naming rules.
            if target == "Repeater/ItemsRepeater.common.cpp":
                target = "Repeater/ItemsRepeaterCommon.cpp"
            blob = normalized(original, path.suffix.lower())
            blobs[target] = blob
            entry = {"source": source, "target": target,
                     "upstream_sha256": digest(original), "staged_sha256": digest(blob)}
            entries.append(entry)
            area_entries.append(entry)
        areas.append({
            "area": area, "state": "source-staged",
            "release_api_review": "pending; a tagged tree also contains preview/system-only definitions",
            "cpp_files": sum(e["target"].endswith(".cpp") for e in area_entries),
            "xaml_files": sum(e["target"].endswith(".xaml") for e in area_entries),
            "files": len(area_entries),
        })
    license_blob = (ROOT / "modules/fluent/LICENSE-WinUI2.3.txt").read_bytes()
    if digest(license_blob) != original_hashes["LICENSE"]:
        raise ValueError("Existing module license does not match upstream")
    manifest = {
        "repository": "https://github.com/microsoft/microsoft-ui-xaml",
        "tag": "v2.3.191129002", "commit": COMMIT,
        "license": "../LICENSE-WinUI2.3.txt", "license_sha256": digest(license_blob),
        "state": "source-staged; not built, installed, packaged or registered",
        "normalization": "UTF-8/LF, trim line ends/final blank lines, expand mixed XAML indentation; one C++ basename normalized",
        "excluded": ["upstream build/packaging", "Generated", "tests/TestUI", "dll activation runtime",
                     "CommonManaged", "Telemetry", "TestHooks", "design tools"],
        "areas": areas, "files": entries,
    }
    blobs[MANIFEST] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    # Validate the entire operation before writing; never replace hand-adapted files.
    for relative, blob in blobs.items():
        target = checked_target(relative)
        if target.exists() and target.read_bytes() != blob:
            raise ValueError(f"Refusing to overwrite changed file: {relative}")
    for relative, blob in blobs.items():
        target = checked_target(relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_bytes(blob)
    check()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=ROOT / "build/reference/winui2.3")
    parser.add_argument("--check", action="store_true", help="Verify staged files without upstream cache")
    args = parser.parse_args()
    if args.check:
        check()
    else:
        stage(args.reference.resolve())


if __name__ == "__main__":
    main()
