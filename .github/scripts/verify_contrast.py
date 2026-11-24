#!/usr/bin/env python3
"""Contrast audit for a generated SVG: every text element against the surface it
actually sits on, in paint order.

Why this exists: a chip that paints a solid `ink` rect and then a translucent
`ink` rect on top looks fine in the code and renders as unreadable text in both
themes — the "white on white / black on black" failure. Eyeballing a browser tab
misses it; this catches it before a README does.

  python3 verify_contrast.py docs/diagrams/deploy.svg
  python3 verify_contrast.py docs/diagrams/*.svg --json report.json

Exit status is non-zero when any text falls below its threshold, so it works as
a CI gate: regular text 4.5:1, text at 24px+ (or 18.7px+ at weight 600) 3:1.
"""

from __future__ import annotations

import argparse
import json
import re
import sys

ELEMENT = re.compile(r"<(rect|text|line|polyline|circle)\b([^>]*)>(?:([^<]*)</\1>)?")
ATTR = re.compile(r'([a-zA-Z-]+)="([^"]*)"')


def parse_attrs(blob: str) -> dict[str, str]:
    return {k: v for k, v in ATTR.findall(blob)}


def parse_color(value: str) -> tuple[float, float, float, float] | None:
    value = (value or "").strip()
    if value.startswith("#"):
        h = value[1:]
        if len(h) == 3:
            h = "".join(c * 2 for c in h)
        if len(h) != 6:
            return None
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) + (1.0,)  # type: ignore[return-value]
    m = re.match(r"rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*(?:,\s*([\d.]+))?\s*\)", value)
    if m:
        r, g, b = (float(m.group(i)) for i in (1, 2, 3))
        a = float(m.group(4)) if m.group(4) is not None else 1.0
        return (r, g, b, a)
    return None


def over(top: tuple[float, float, float, float], bottom: tuple[float, float, float, float]):
    a = top[3] + bottom[3] * (1 - top[3])
    if a == 0:
        return (0.0, 0.0, 0.0, 0.0)
    return tuple((top[i] * top[3] + bottom[i] * bottom[3] * (1 - top[3])) / a for i in range(3)) + (a,)


def luminance(c: tuple[float, float, float, float]) -> float:
    def ch(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * ch(c[0]) + 0.7152 * ch(c[1]) + 0.0722 * ch(c[2])


def ratio(a, b) -> float:
    la, lb = luminance(a), luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def bbox_of(kind: str, a: dict[str, str], text: str) -> tuple[float, float, float, float] | None:
    try:
        if kind == "rect":
            # x/y default to 0: the canvas background rect carries only w/h
            x, y = float(a.get("x", 0)), float(a.get("y", 0))
            return (x, y, x + float(a["width"]), y + float(a["height"]))
        if kind == "text":
            size = float(a.get("font-size", 12))
            weight = int(a.get("font-weight", 400))
            mono = "Mono" in a.get("font-family", "") or "monospace" in a.get("font-family", "")
            adv = size * (0.60 if mono else 0.55)
            w = len(text) * adv
            x, y = float(a["x"]), float(a["y"])
            anchor = a.get("text-anchor", "start")
            if anchor == "middle":
                x -= w / 2
            elif anchor == "end":
                x -= w
            return (x, y - size * 0.78, x + w, y + size * 0.24)
        if kind == "line":
            x1, y1, x2, y2 = (float(a[k]) for k in ("x1", "y1", "x2", "y2"))
            return (min(x1, x2), min(y1, y2), max(x1, x2) or min(x1, x2) + 1, max(y1, y2) or min(y1, y2) + 1)
        if kind == "polyline":
            pts = [tuple(map(float, p.split(","))) for p in a.get("points", "").split()]
            if not pts:
                return None
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            return (min(xs), min(ys), max(xs), max(ys))
        if kind == "circle":
            cx, cy, r = float(a["cx"]), float(a["cy"]), float(a["r"])
            return (cx - r, cy - r, cx + r, cy + r)
    except (KeyError, ValueError):
        return None
    return None


def covers(box, x: float, y: float) -> bool:
    return box[0] <= x <= box[2] and box[1] <= y <= box[3]


def audit(path: str, samples: int = 5) -> list[dict]:
    svg = open(path, encoding="utf-8").read()
    body = svg.split("</defs>")[-1].split("</svg>")[0]
    layers: list[dict] = []
    for m in ELEMENT.finditer(body):
        kind, blob, text = m.group(1), m.group(2), (m.group(3) or "")
        a = parse_attrs(blob)
        box = bbox_of(kind, a, text)
        if box is None:
            continue
        fill = parse_color(a.get("fill", ""))
        opacity = float(a.get("fill-opacity", 1) or 1)
        if fill and opacity < 1:
            fill = fill[:3] + (fill[3] * opacity,)
        layers.append({"kind": kind, "attrs": a, "box": box, "fill": fill, "text": text})

    canvas = layers[0]["fill"] if layers and layers[0]["kind"] == "rect" and layers[0]["fill"] else (255, 255, 255, 1)
    problems = []
    for i, layer in enumerate(layers):
        if layer["kind"] != "text" or not layer["fill"]:
            continue
        size = float(layer["attrs"].get("font-size", 12))
        weight = int(layer["attrs"].get("font-weight", 400))
        large = size >= 24 or (size >= 18.7 and weight >= 600)
        threshold = 3.0 if large else 4.5
        x0, y0, x1, y1 = layer["box"]
        worst = (99.0, None)
        for k in range(samples):
            px = x0 + (x1 - x0) * (k + 0.5) / samples
            py = y0 + (y1 - y0) * 0.75
            bg = canvas
            for below in layers[:i]:
                if below["fill"] and below["kind"] != "text" and covers(below["box"], px, py):
                    bg = over(below["fill"], bg)
            if bg[3] < 1:  # composite the remainder over the canvas
                bg = over(bg, canvas)
            r = ratio(layer["fill"], bg)
            if r < worst[0]:
                worst = (r, bg)
        if worst[0] < threshold:
            problems.append({"text": layer["text"][:48], "size": size,
                             "ratio": round(worst[0], 2), "threshold": threshold,
                             "background": "#%02x%02x%02x" % tuple(int(v) for v in worst[1][:3])})
    return problems


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("svgs", nargs="+")
    ap.add_argument("--json", help="write the full report here")
    a = ap.parse_args()
    report, failed = {}, False
    for path in a.svgs:
        problems = audit(path)
        report[path] = problems
        if problems:
            failed = True
            print(f"FAIL {path}")
            for p in problems[:12]:
                print(f"  {p['ratio']}:1 (needs {p['threshold']}) on {p['background']}  "
                      f"{p['size']}px  \"{p['text']}\"")
        else:
            print(f"ok   {path}")
    if a.json:
        json.dump(report, open(a.json, "w", encoding="utf-8"), indent=2)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
