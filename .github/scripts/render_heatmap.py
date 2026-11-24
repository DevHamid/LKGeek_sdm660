#!/usr/bin/env python3
"""Render a commit/issue activity heatmap SVG from git history.

Deterministic: the grid is derived only from the commit dates, and the last
column ends on the newest commit date unless --end is passed. No clock read,
so two runs on the same history produce byte-identical files.

  python3 render_heatmap.py --git /path/to/repo --out docs/diagrams/activity.svg --weeks 26
  python3 render_heatmap.py --git . --out activity-dark.svg --theme dark

Requires fetch-depth: 0 in CI, or the grid renders empty (shallow clone = 1 commit).
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svgkit as k  # noqa: E402

PITCH = 20          # 16px cell + 4px gap, on the 4px grid
LEFT = 32
TOP = 96
DAYS = ["Mon", "Wed", "Fri"]
BUCKETS = [(0, 0), (1, 2), (3, 5), (6, 9)]  # last bucket is open-ended
LEGEND = ["0", "1-2", "3-5", "6-9", "10+"]


def commit_counts(repo: str) -> dict[str, int]:
    out = subprocess.run(
        ["git", "-C", repo, "log", "--format=%ad", "--date=short", "--no-merges"],
        capture_output=True, text=True, check=True,
    ).stdout
    counts: dict[str, int] = {}
    for line in out.splitlines():
        line = line.strip()
        if line:
            counts[line] = counts.get(line, 0) + 1
    if not counts:
        raise SystemExit("no commits found - is this a repo, and did you fetch full history?")
    return counts


def level(n: int) -> int:
    if n <= 0:
        return 0
    for i, (lo, hi) in enumerate(BUCKETS):
        if lo <= n <= hi:
            return i + 1
    return 4


def build(counts: dict[str, int], weeks: int, end: dt.date | None) -> tuple[list[list[dt.date]], dict]:
    newest = max(dt.date.fromisoformat(d) for d in counts)
    end = end or newest
    # end the grid on the Sunday closing that week
    end += dt.timedelta(days=(6 - end.weekday()))
    start = end - dt.timedelta(days=weeks * 7 - 1)
    cols = [[start + dt.timedelta(days=w * 7 + d) for d in range(7)] for w in range(weeks)]
    meta = {
        "total": sum(counts.values()),
        "active_days": sum(1 for v in counts.values() if v),
        "peak": max(counts.values()),
        "first": min(counts),
        "last": max(counts),
        "start": start.isoformat(),
        "end": end.isoformat(),
    }
    return cols, meta


def render(counts, weeks, theme, title_text, end=None) -> str:
    th = k.THEMES[theme]
    cols, meta = build(counts, weeks, end)
    width = k.snap(LEFT + weeks * PITCH + 32)
    height = k.snap(TOP + 7 * PITCH + 72)

    parts: list[str] = []
    parts.append(k.line(LEFT, TOP - 8, k.snap(LEFT + weeks * PITCH - 4), TOP - 8, theme=th, stroke=th["rule-solid"]))

    last_month = None
    for w, col in enumerate(cols):
        x = LEFT + w * PITCH
        month = col[0].strftime("%b")
        if month != last_month and col[0].day <= 7 and (w == 0 or col[0].month != cols[w - 1][0].month):
            parts.append(k.text(x, TOP - 14, month, theme=th, size=10, font="mono", fill=th["muted"]))
            last_month = month
        for d, day in enumerate(col):
            n = counts.get(day.isoformat(), 0)
            lv = level(n)
            fill = th["levels"][lv]
            stroke = th["rule"] if lv == 0 else "none"
            y = TOP + d * PITCH
            tip = f"{day.isoformat()}: {n} commit" + ("" if n == 1 else "s")
            parts.append(
                f'<rect x="{x}" y="{k.snap(y)}" width="16" height="16" rx="3" fill="{fill}"'
                + (f' stroke="{stroke}" stroke-width="1"' if stroke != "none" else "")
                + f'><title>{k.esc(tip)}</title></rect>'
            )

    for i, label in enumerate(DAYS):
        row = i * 2  # Mon, Wed, Fri
        parts.append(k.text(LEFT - 8, TOP + row * PITCH + 11, label, theme=th, size=10,
                            font="mono", fill=th["muted"], anchor="end"))

    # legend
    ly = TOP + 7 * PITCH + 28
    parts.append(k.text(LEFT, ly + 11, "less", theme=th, size=10, font="mono", fill=th["muted"]))
    x = LEFT + 36
    for i, fill in enumerate(th["levels"]):
        parts.append(f'<rect x="{x + i * PITCH}" y="{k.snap(ly)}" width="16" height="16" rx="3" fill="{fill}"'
                     + (f' stroke="{th["rule"]}" stroke-width="1"' if i == 0 else "")
                     + f'><title>{k.esc(LEGEND[i] + " commits/day")}</title></rect>')
    parts.append(k.text(x + 5 * PITCH + 4, ly + 11, "more", theme=th, size=10, font="mono", fill=th["muted"]))

    stats = f"{meta['total']} commits · {meta['active_days']} active days · peak {meta['peak']}/day · {meta['start']} → {meta['last']}"
    parts.append(k.text(24, height - 16, stats, theme=th, size=11, font="mono", fill=th["soft"]))

    desc = (f"Commit activity per day from {meta['start']} to {meta['end']}: "
            f"{meta['total']} commits across {meta['active_days']} active days, peak {meta['peak']} in one day.")
    body = "\n  ".join(parts)
    return k.frame(width, height, "  " + body, theme=theme, title_text=title_text,
                   eyebrow_text="activity · repo-docs-pipeline", slug="activity", desc=desc)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--git", default=".")
    ap.add_argument("--out", required=True)
    ap.add_argument("--weeks", type=int, default=26)
    ap.add_argument("--theme", choices=["light", "dark"], default="light")
    ap.add_argument("--title", default="Commit activity")
    ap.add_argument("--end", help="ISO date to end the grid on (default: newest commit)")
    a = ap.parse_args()
    counts = commit_counts(a.git)
    end = dt.date.fromisoformat(a.end) if a.end else None
    k.write(a.out, render(counts, a.weeks, a.theme, a.title, end))
    print(f"{a.out}: {sum(counts.values())} commits, {a.weeks} weeks, theme={a.theme}")


if __name__ == "__main__":
    main()
