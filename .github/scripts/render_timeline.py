#!/usr/bin/env python3
"""Render a milestone timeline SVG from git tags (or a milestones JSON).

Deterministic: tags are ordered by creation date then name, so the axis is
stable for a given history.

  python3 render_timeline.py --git . --out docs/diagrams/timeline.svg --last 10
  python3 render_timeline.py --src docs/data/milestones.json --out timeline.svg
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svgkit as k  # noqa: E402

WIDTH = 960
TOP = 132


TAG_RE = re.compile(r"^[A-Za-z]?\d+\.\d+")
SUBJECT_MAX = 40


def from_git(repo: str, last: int, match: str | None) -> list[dict]:
    fmt = "%(refname:short)\t%(creatordate:short)\t%(subject)"
    out = subprocess.run(["git", "-C", repo, "for-each-ref", "--sort=creatordate",
                          f"--format={fmt}", "refs/tags"],
                         capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) < 2 or not parts[1]:
            continue
        tag = parts[0]
        if match and not re.match(match, tag):
            continue
        rows.append({"tag": tag, "date": parts[1],
                     "subject": (parts[2] if len(parts) > 2 else "")[:SUBJECT_MAX]})
    if not rows:  # nothing matched the release pattern: fall back to every tag,
        # but only once - recursing on a repo with no tags at all never terminates
        return [] if match is None else from_git(repo, last, None)
    return rows[-last:] if last else rows


def from_json(path: str) -> list[dict]:
    data = json.load(open(path, encoding="utf-8"))
    rows = data["milestones"] if isinstance(data, dict) else data
    return sorted(rows, key=lambda r: (r["date"], r["tag"]))


def render(rows, theme, title_text, subtitle) -> str:
    th = k.THEMES[theme]
    n = len(rows)
    if n == 0:
        raise SystemExit("no milestones to draw")
    width = WIDTH
    height = k.snap(TOP + 190)
    left, right = 56, width - 56
    span = right - left
    step = span / max(n - 1, 1)
    axis_y = TOP + 60

    parts: list[str] = []
    if subtitle:
        parts.append(k.text(24, 90, subtitle, theme=th, size=12, fill=th["muted"]))

    first = dt.date.fromisoformat(rows[0]["date"])
    last = dt.date.fromisoformat(rows[-1]["date"])
    days = max((last - first).days, 1)
    parts.append(k.line(left, axis_y, right, axis_y, theme=th, stroke=th["rule-solid"], width=1))
    parts.append(k.text(left, axis_y + 20, f"{rows[0]['date']}", theme=th, size=10, font="mono",
                        fill=th["soft"]))
    parts.append(k.text(right, axis_y + 20, f"{rows[-1]['date']} · {days} days", theme=th,
                        size=10, font="mono", fill=th["soft"], anchor="end"))

    for i, row in enumerate(rows):
        x = left + i * step
        focal = i == n - 1  # accent reserved for the newest milestone
        above = i % 2 == 0
        r = 6 if focal else 4
        parts.append(k.line(x, axis_y - 12, x, axis_y + 12, theme=th, stroke=th["rule"]))
        parts.append(k.circle(x, axis_y, r, theme=th,
                              fill=th["accent"] if focal else th["paper"],
                              stroke=th["accent"] if focal else th["muted"]))
        ty = axis_y - 28 if above else axis_y + 44
        anchor = "middle"
        if i == 0:
            anchor = "start"
        elif i == n - 1:
            anchor = "end"
        parts.append(k.text(x, ty, row["tag"], theme=th, size=12, font="mono", weight=500,
                            fill=th["accent"] if focal else th["ink"], anchor=anchor))
        y2 = ty - 16 if above else ty + 16
        parts.append(k.text(x, y2, row["date"], theme=th, size=10, font="mono",
                            fill=th["soft"], anchor=anchor))
        if row.get("subject") and n <= 12:
            # keep the label inside its slot: an 11px sans glyph averages ~6px,
            # and two labels sharing a y-band must not run into each other
            max_chars = int((step - 10) / 6.0)
            label = row["subject"]
            if len(label) > max_chars >= 12:
                label = label[:max_chars - 1].rstrip(" ,;:-") + "\u2026"
            if max_chars >= 12:
                y3 = y2 - 14 if above else y2 + 14
                parts.append(k.text(x, y3, label, theme=th, size=11,
                                    fill=th["muted"], anchor=anchor))

    desc = f"Milestone timeline: {n} tags from {rows[0]['tag']} ({rows[0]['date']}) to {rows[-1]['tag']} ({rows[-1]['date']})."
    body = "\n  ".join(parts)
    return k.frame(width, height, "  " + body, theme=theme, title_text=title_text,
                   eyebrow_text="timeline · repo-docs-pipeline", slug="timeline", desc=desc)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--git")
    ap.add_argument("--src")
    ap.add_argument("--out", required=True)
    ap.add_argument("--last", type=int, default=8, help="keep the newest N tags (0 = all)")
    ap.add_argument("--theme", choices=["light", "dark"], default="light")
    ap.add_argument("--title", default="Milestones")
    ap.add_argument("--subtitle", default="")
    ap.add_argument("--match", default=r"^[A-Za-z]?\d+\.\d+",
                    help="tag regex to keep (default: semver-like); empty string = all tags")
    a = ap.parse_args()
    if not a.git and not a.src:
        raise SystemExit("pass --git or --src")
    rows = from_json(a.src) if a.src else from_git(a.git, a.last, a.match or None)
    k.write(a.out, render(rows, a.theme, a.title, a.subtitle))
    print(f"{a.out}: {len(rows)} milestones ({rows[0]['tag']} .. {rows[-1]['tag']})")


if __name__ == "__main__":
    main()
