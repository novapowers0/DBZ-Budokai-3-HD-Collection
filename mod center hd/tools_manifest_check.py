#!/usr/bin/env python3
"""Validate the curated HD modding-tools manifest.

This checker only validates metadata and paths. It never runs external tools or
modifies assets, so it is safe to use before packaging a release.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


REQUIRED_CATEGORIES = ("hd", "bridge", "reference")
REQUIRED_ENTRY_KEYS = ("path", "kind", "status", "formats")


def load_manifest(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        data = json.load(stream)
    if not isinstance(data, dict):
        raise ValueError("manifest root must be an object")
    return data


def validate(manifest_path: Path, data: dict, package: bool = False) -> list[str]:
    errors: list[str] = []
    categories = data.get("categories")
    if not isinstance(categories, dict):
        return ["categories must be an object"]

    for category in REQUIRED_CATEGORIES:
        entries = categories.get(category)
        if not isinstance(entries, list):
            errors.append(f"category {category!r} must be a list")
            continue
        for index, entry in enumerate(entries):
            prefix = f"{category}[{index}]"
            if not isinstance(entry, dict):
                errors.append(f"{prefix} must be an object")
                continue
            for key in REQUIRED_ENTRY_KEYS:
                if key not in entry:
                    errors.append(f"{prefix} missing {key!r}")
            formats = entry.get("formats")
            if not isinstance(formats, list) or not formats:
                errors.append(f"{prefix}.formats must be a non-empty list")

            relative = entry.get("path")
            if not isinstance(relative, str) or not relative:
                errors.append(f"{prefix}.path must be a non-empty string")
                continue
            if "package_path" in entry:
                package_path = entry["package_path"]
                if package_path is not None and (not isinstance(package_path, str) or not package_path):
                    errors.append(f"{prefix}.package_path must be a non-empty string or null")
                    continue
                if package:
                    if package_path is None:
                        continue
                    relative = package_path
            target = (manifest_path.parent / relative).resolve()
            if not target.exists():
                errors.append(f"{prefix} path does not exist: {relative}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "manifest",
        nargs="?",
        type=Path,
        default=Path(__file__).with_name("tools_manifest.json"),
    )
    parser.add_argument("--package", action="store_true",
                        help="validate shipped paths; null package_path entries are repository-only")
    args = parser.parse_args()

    try:
        data = load_manifest(args.manifest)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    errors = validate(args.manifest, data, package=args.package)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    entries = sum(len(data["categories"][category]) for category in REQUIRED_CATEGORIES)
    scope = "package" if args.package else "repository"
    print(f"OK: {args.manifest} ({entries} entries, {scope} metadata and paths valid)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
