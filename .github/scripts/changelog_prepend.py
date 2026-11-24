#!/usr/bin/env python3
"""Prepend a generated release block to CHANGELOG.md. Never rewrites older blocks.

Insertion point: immediately before the first `## ` version heading, so the file's
intro (title, links, Keep-a-Changelog note) stays on top. Idempotent: if the same
version heading already exists anywhere in the file, nothing is written.

  python3 changelog_prepend.py --notes /tmp/notes.md --file CHANGELOG.md
"""

from __future__ import annotations

import argparse
import re
import sys

HEADING = re.compile(r"^##\s+(?:\[)?(?:v)?(?P<v>\d+\.\d+(?:\.\d+)?[^\]\s]*)", re.M)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--notes", required=True, help="markdown block produced by changelog_local.py")
    ap.add_argument("--file", default="CHANGELOG.md")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    block = open(a.notes, encoding="utf-8").read().strip()
    if not block.startswith("## "):
        sys.exit("notes do not start with a '## <version>' heading - refusing to prepend")

    new_version = HEADING.match(block)
    if not new_version:
        sys.exit("could not read a version from the notes heading")

    try:
        existing = open(a.file, encoding="utf-8").read()
    except FileNotFoundError:
        existing = "# Changelog\n\nAll notable changes to this project.\n\n"

    versions = {m.group("v").lstrip("v") for m in HEADING.finditer(existing)}
    if new_version.group("v").lstrip("v") in versions:
        print(f"{a.file}: {new_version.group('v')} already present, nothing to do")
        return

    # insert before the first version heading; append if the file has none
    first = HEADING.search(existing)
    if first:
        head, tail = existing[:first.start()].rstrip("\n"), existing[first.start():].lstrip("\n")
        merged = f"{head}\n\n{block}\n\n{tail}"
    else:
        merged = f"{existing.rstrip()}\n\n{block}\n"

    if a.dry_run:
        print(merged[:1200])
        print(f"... ({len(merged)} bytes, dry run)")
        return

    open(a.file, "w", encoding="utf-8").write(merged)
    print(f"{a.file}: prepended {new_version.group('v')} ({len(block)} bytes)")


if __name__ == "__main__":
    main()
