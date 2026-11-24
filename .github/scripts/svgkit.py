"""Shared style tokens and SVG helpers for repo README assets.

Design rules carried from the editorial system (see references/readme-svg-assets.md):
  - 4px grid for every coordinate, 1px hairlines, no shadows, radius 0-6px
  - accent on 1-2 elements only
  - system font stacks only: <img>-embedded SVG makes no network requests,
    so any webfont silently falls back and shifts metrics
  - same input -> byte-identical output (no timestamps unless the caller passes one)
"""

from __future__ import annotations

from xml.sax.saxutils import escape

GRID = 4

FONTS = {
    "sans": "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif",
    "serif": "Georgia, 'Times New Roman', 'Liberation Serif', serif",
    "mono": "ui-monospace, SFMono-Regular, Menlo, Consolas, 'Liberation Mono', monospace",
}

THEMES = {
    "light": {
        "paper": "#f5f5f5",
        "paper-2": "#ececec",
        "ink": "#2d3142",
        "muted": "#4f5d75",
        "soft": "#5a6580",
        "rule": "rgba(45,49,66,0.12)",
        "rule-solid": "#bfc0c0",
        "accent": "#bf4520",
        "accent-tint": "rgba(191,69,32,0.08)",
        "link": "#2e5aa8",
        "levels": ["#e4e4e4", "#f0c9b6", "#e39a72", "#d1693c", "#bf4520"],
    },
    "dark": {
        "paper": "#2d3142",
        "paper-2": "#393e53",
        "ink": "#f5f5f5",
        "muted": "#bfc0c0",
        "soft": "#b0b8c6",
        "rule": "rgba(245,245,245,0.12)",
        "rule-solid": "rgba(191,192,192,0.25)",
        "accent": "#f08a59",
        "accent-tint": "rgba(240,138,89,0.10)",
        "link": "#739fdf",
        "levels": ["#3b4056", "#6b4a3d", "#95614a", "#c47a52", "#f08a59"],
    },
}


def snap(v: float) -> int:
    """Snap a coordinate to the 4px grid."""
    return int(round(v / GRID) * GRID)


def esc(text: str) -> str:
    return escape(str(text), {'"': "&quot;", "'": "&apos;"})


def text(x, y, s, *, theme, size=12, font="sans", weight=400, fill=None, anchor="start",
         tracking=None, opacity=None, italic=False):
    fill = fill or theme["ink"]
    attrs = [
        f'x="{snap(x)}"', f'y="{snap(y)}"',
        f'font-family="{FONTS[font]}"', f'font-size="{size}"',
        f'text-anchor="{anchor}"',
    ]
    if weight != 400:
        attrs.append(f'font-weight="{weight}"')
    if tracking is not None:
        attrs.append(f'letter-spacing="{tracking}"')
    if opacity is not None:
        attrs.append(f'opacity="{opacity}"')
    if italic:
        attrs.append('font-style="italic"')
    attrs.append(f'fill="{fill}"')
    return f'<text {" ".join(attrs)}>{esc(s)}</text>'


def eyebrow(x, y, s, *, theme, color=None):
    return text(x, y, s.upper(), theme=theme, size=10, font="mono", weight=500,
                fill=color or theme["muted"], tracking="1.8")


def title(x, y, s, *, theme, size=26):
    return text(x, y, s, theme=theme, size=size, font="serif", fill=theme["ink"])


def rect(x, y, w, h, *, theme, fill="none", stroke=None, stroke_width: float = 1, radius=0):
    parts = [f'x="{snap(x)}"', f'y="{snap(y)}"', f'width="{snap(w)}"', f'height="{snap(h)}"']
    if radius:
        parts.append(f'rx="{radius}"')
    parts.append(f'fill="{fill}"')
    if stroke:
        parts.append(f'stroke="{stroke}" stroke-width="{stroke_width}"')
    return f'<rect {" ".join(parts)}/>'


def line(x1, y1, x2, y2, *, theme, stroke=None, width=1, dash=None, opacity=None):
    parts = [f'x1="{snap(x1)}"', f'y1="{snap(y1)}"', f'x2="{snap(x2)}"', f'y2="{snap(y2)}"',
             f'stroke="{stroke or theme["rule"]}"', f'stroke-width="{width}"']
    if dash:
        parts.append(f'stroke-dasharray="{dash}"')
    if opacity is not None:
        parts.append(f'opacity="{opacity}"')
    return f'<line {" ".join(parts)}/>'


def circle(cx, cy, r, *, theme, fill, stroke=None, width=1):
    s = f'cx="{snap(cx)}" cy="{snap(cy)}" r="{r}" fill="{fill}"'
    if stroke:
        s += f' stroke="{stroke}" stroke-width="{width}"'
    return f"<circle {s}/>"


def frame(width, height, body, *, theme, title_text, eyebrow_text, slug, desc):
    """Wrap body elements in a standalone, GitHub-safe SVG document."""
    th = THEMES[theme]
    head = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
        f'width="{width}" height="{height}" role="img" '
        f'aria-labelledby="{slug}-t {slug}-d">',
        f'<title id="{slug}-t">{esc(title_text)}</title>',
        f'<desc id="{slug}-d">{esc(desc)}</desc>',
        f'<rect width="{width}" height="{height}" fill="{th["paper"]}"/>',
        eyebrow(24, 34, eyebrow_text, theme=th),
        title(24, 68, title_text, theme=th),
    ]
    return "\n".join(head + [body] + ["</svg>", ""])


def write(path: str, content: str) -> None:
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(content)
