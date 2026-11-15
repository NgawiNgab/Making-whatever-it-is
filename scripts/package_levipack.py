#!/usr/bin/env python3
"""Builds a .levipack from a compiled library, an icon and the version header.

The manifest format is fixed by the preloader's installer, so this mirrors the
reference packager (BedrockTools scripts/package_levipack.py) rather than inventing
a layout: `type`, `name`, `author`, `description`, `version`, `entry`, `icon`,
`overwrite_files`, `overwrite_folders`.

Deliberate difference from the reference: the reference hardcodes `libBedrockTools.so`
in both the manifest and the archive. Here the entry name is derived from one
constant so the manifest can never name a file the archive does not contain — a
mismatch that would install cleanly and fail at load.

Usage:
    scripts/package_levipack.py --library build/libCameraFacing.so \
        --icon assets/icon.png --output build/CameraFacing.levipack
"""

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

VALUE_PATTERN = re.compile(
    r'^\s*inline\s+constexpr\s+std::string_view\s+'
    r'(Name|Author|Description|Version)\s*=\s*"((?:\\.|[^"\\])*)";\s*$'
)
REQUIRED_VALUES = ("Name", "Author", "Description", "Version")

ENTRY_NAME = "libCameraFacing.so"
ICON_NAME = "icon.png"


def parse_version(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = VALUE_PATTERN.match(line)
        if match:
            values[match.group(1)] = bytes(match.group(2), "utf-8").decode("unicode_escape")
    missing = [name for name in REQUIRED_VALUES if not values.get(name)]
    if missing:
        raise ValueError("missing version metadata: " + ", ".join(missing))
    return values


def build_manifest(values: dict[str, str]) -> dict[str, object]:
    return {
        "type": "preload-native",
        "name": values["Name"],
        "author": values["Author"],
        "description": values["Description"],
        "version": values["Version"],
        "entry": ENTRY_NAME,
        "icon": ICON_NAME,
        "overwrite_files": [ICON_NAME],
        "overwrite_folders": [],
    }


def write_package(library: Path, icon: Path, version_header: Path, output: Path) -> None:
    for label, path in (
        ("library", library),
        ("icon", icon),
        ("version header", version_header),
    ):
        if not path.is_file():
            raise FileNotFoundError(f"{label} not found: {path}")

    if library.name != ENTRY_NAME:
        raise ValueError(
            f"library is named {library.name!r} but the manifest declares {ENTRY_NAME!r}; "
            "rename the artifact or update ENTRY_NAME"
        )

    manifest = build_manifest(parse_version(version_header))
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()

    manifest_bytes = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr("manifest.json", manifest_bytes)
        archive.write(library, ENTRY_NAME)
        archive.write(icon, ICON_NAME)

    # Verify by reading back: a package whose manifest names entries it does not
    # contain installs and then fails, which is the worst place to find out.
    with zipfile.ZipFile(output, "r") as archive:
        names = set(archive.namelist())
        for required in ("manifest.json", ENTRY_NAME, ICON_NAME):
            if required not in names:
                raise RuntimeError(f"package is missing {required}")

    print(f"wrote {output} ({output.stat().st_size} bytes)")
    print(f"  name    {manifest['name']}")
    print(f"  version {manifest['version']}")
    print(f"  entry   {ENTRY_NAME}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--icon", type=Path, required=True)
    parser.add_argument(
        "--version-header",
        type=Path,
        default=Path("include/camface/Version.hpp"),
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    try:
        write_package(args.library, args.icon, args.version_header, args.output)
    except (ValueError, FileNotFoundError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
