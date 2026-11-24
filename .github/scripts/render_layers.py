#!/usr/bin/env python3
"""Render a layer-stack SVG: hierarchy as full-width bands, top = most abstract.

For a kernel fork the stack is the provenance chain — hardware, upstream base,
vendor deltas, patchset, config, build artifact — which no architecture or
deployment diagram can express.

Source JSON (docs/data/layers.json):

{
  "title": "...", "subtitle": "...", "direction": "abstraction ↑",
  "layers": [{"tag": "L6", "name": "...", "note": "...", "accent": true}, ...]
}

  python3 render_layers.py --src docs/data/layers.json --out docs/diagrams/layers.svg
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svgkit as k  # noqa: E402

BAND_H, GAP = 60, 4
LEFT, RIGHT = 24, 936
MAX_LAYERS = 6
ACCENT_BUDGET = 1  # one focal layer: the pay-rent layer, not a rainbow


def render(src: dict, theme: str) -> str:
    th = k.THEMES[theme]
    layers = src["layers"][:MAX_LAYERS]
    top = 132
    height = k.snap(top + len(layers) * (BAND_H + GAP) + 76)
    width = 960

    out: list[str] = []
    if src.get("subtitle"):
        out.append(k.text(LEFT, 96, src["subtitle"], theme=th, size=12, fill=th["muted"]))

    # direction indicator, horizontal on purpose: rotated text is unreadable
    out.append(k.text(RIGHT, 96, src.get("direction", "abstraction ↑"), theme=th, size=9,
                      font="mono", fill=th["soft"], anchor="end"))

    accents = 0
    for i, layer in enumerate(layers):
        y = top + i * (BAND_H + GAP)
        focal = bool(layer.get("accent")) and accents < ACCENT_BUDGET
        if focal:
            accents += 1
        fill = th["accent-tint"] if focal else (th["paper-2"] if i % 2 else th["paper"])
        stroke = th["accent"] if focal else th["rule"]
        out.append(f'<rect x="{k.snap(LEFT)}" y="{k.snap(y)}" width="{RIGHT - LEFT}" height="{BAND_H}" '
                   f'rx="4" fill="{fill}" stroke="{stroke}" stroke-width="1"/>')
        out.append(k.text(LEFT + 16, y + 26, layer.get("tag", f"L{len(layers)-i:02d}"), theme=th,
                          size=9, font="mono", weight=500,
                          fill=th["ink"] if focal else th["soft"], tracking="1.2"))
        out.append(k.text(LEFT + 88, y + 36, layer["name"], theme=th, size=15, weight=600,
                          fill=th["ink"]))
        if layer.get("note"):
            out.append(k.text(RIGHT - 16, y + 35, layer["note"], theme=th, size=10, font="mono",
                              fill=th["muted"], anchor="end"))

    if len(src["layers"]) > MAX_LAYERS:
        out.append(k.text(LEFT, height - 34,
                          f"{len(src['layers'])} layers supplied, {MAX_LAYERS} drawn — split the stack",
                          theme=th, size=9, font="mono", fill=th["accent"]))

    foot = src.get("footnote")
    if foot:
        out.append(k.text(LEFT, height - 20, foot, theme=th, size=10, font="mono", fill=th["soft"]))

    desc = "Layer stack: " + ", ".join(l["name"] for l in layers) + "."
    body = "\n  ".join(out)
    return k.frame(width, height, "  " + body, theme=theme,
                   title_text=src.get("title", "Layers"), eyebrow_text="layers · readme-diagrams",
                   slug="layers", desc=desc)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--theme", choices=["light", "dark"], default="light")
    a = ap.parse_args()
    src = json.load(open(a.src, encoding="utf-8"))
    k.write(a.out, render(src, a.theme))
    print(f"{a.out}: {len(src['layers'])} layers, theme={a.theme}")


if __name__ == "__main__":
    main()
