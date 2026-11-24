#!/usr/bin/env python3
"""Local dry-run of the changelog: git log -> markdown, no network, no npm.

Purpose: preview exactly what the CI engine will publish, before the workflow
runs. Uses the shared type map from references/changelog-engines.md.

Handles a repo whose history is only partly conventional: an unknown prefix is
either mapped by --legacy-map or collected under one fallback section, and the
count is reported on stderr so the gap is visible instead of silent.

  python3 changelog_local.py --git . --from v0.9.16 --to HEAD --repo owner/name
  python3 changelog_local.py --git . --from <first-commit> --legacy-map 2410=build,CI=ci,README=docs
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys

TYPES = [  # shared map, in changelogen order; upstream first for fork repos
    ("upstream", "⬆️ Upstream intake"),
    ("feat", "🚀 Enhancements"),
    ("fix", "🩹 Fixes"),
    ("perf", "🔥 Performance"),
    ("refactor", "💅 Refactors"),
    ("docs", "📖 Documentation"),
    ("build", "📦 Build"),
    ("types", "🌊 Types"),
    ("test", "✅ Tests"),
    ("style", "🎨 Styles"),
    ("ci", "🤖 CI"),
    ("examples", "🏀 Examples"),
    ("chore", "🏡 Chore"),
    ("kernel", "🔧 Subsystem changes"),
    ("other", "🧩 Other"),
    ("revert", "↩️ Reverts & re-applies"),
]
TITLE = dict(TYPES)
ORDER = {t: i for i, (t, _) in enumerate(TYPES)}

# Scope icons are informational, not decorative: the section header already
# carries the type, so the per-entry icon carries the subsystem instead.
SCOPE_ICONS = {
    "kernel": "⚙️", "dts": "🌳", "wifi": "📶", "wlan": "📶", "qcacld": "📶",
    "modem": "📡", "radio": "📡", "ipa": "🛰️", "rmnet": "🛰️", "data": "🛰️",
    "glink": "🔗", "qrtr": "🔗", "qmi": "🔗", "usb": "🔌", "gadget": "🔌",
    "screen": "🖥️", "display": "🖥️", "ota": "⬆️", "build": "📦", "rom": "📦",
    "2410": "📦", "ci": "🤖", "docs": "📖", "readme": "📖", "net": "🌐",
    "docker": "🐳", "power": "🔋", "vbus": "🔋", "recovery": "🛟", "boot": "🛟",
}
SCOPE_ICON_FALLBACK = "🏷️"

REVERT = re.compile(r'^(Revert|Reapply)\s+"', re.I)
UPSTREAM = re.compile(r'^(UPSTREAM|BACKPORT|FROMGIT|FROMLIST|ANDROID|CHERRY-PICK|CHERRY):\s*', re.I)
SUBSYS = re.compile(r'^([A-Za-z0-9_+./-]{2,24}(?:,\s*[A-Za-z0-9_+./-]{2,24})*):\s+')

BRACKET = re.compile(r"^(?:\[[A-Za-z][^\]]{0,20}\]\s*|(?:squash|fixup)!\s*)+")
BRACKET_UPSTREAM = re.compile(r"^\[(?:backport|upstream|fromgit|fromlist)\]", re.I)

CONV = re.compile(r"^(?P<type>[a-zA-Z]+)(?:\((?P<scope>[^)]*)\))?(?P<bang>!)?:\s*(?P<subject>.+)$")
PREFIX = re.compile(r"^([A-Za-z0-9][\w.\-]*)\s*[:\s]\s*(.+)$")


def log(repo: str, rng: list[str]) -> list[dict]:
    sep, rec = "\x1f", "\x1e"
    fmt = sep.join(["%H", "%h", "%ad", "%s", "%b", "%an"]) + rec
    out = subprocess.run(["git", "-C", repo, "log", f"--format={fmt}", "--date=short",
                          "--no-merges", *rng],
                         capture_output=True, text=True, check=True).stdout
    commits = []
    for chunk in out.split(rec):
        f = chunk.strip("\n").split(sep)
        if len(f) < 6:
            continue
        commits.append({"sha": f[0], "short": f[1], "date": f[2], "subject": f[3],
                        "body": f[4], "author": f[5].strip()})
    return commits


def strip_upstream(subject: str) -> tuple[str, bool]:
    """'BACKPORT: FROMGIT: net: ...' -> ('net: ...', True), chained tags removed."""
    hit = False
    while True:
        u = UPSTREAM.match(subject)
        if not u:
            return subject, hit
        subject = subject[u.end():]
        hit = True


def classify(c: dict, legacy: dict[str, str], fallback: str, kernel_sections: bool = False) -> dict:
    if REVERT.match(c["subject"]):
        return {**c, "type": "revert", "scope": None,
                "subject": REVERT.sub("", c["subject"]).rstrip('"'), "breaking": False}

    subject, was_upstream = strip_upstream(c["subject"])
    if BRACKET_UPSTREAM.match(subject):
        was_upstream = True
    subject = BRACKET.sub("", subject).strip() or subject
    if was_upstream:
        return {**c, "type": "upstream", "scope": None, "subject": subject,
                "breaking": "BREAKING CHANGE" in c["body"].upper()}

    m = CONV.match(subject)
    if m:
        t = m.group("type").lower()
        if t in TITLE:
            scope, subject = m.group("scope"), m.group("subject")
        else:
            # 'wlan: ...' looks conventional but the prefix is a subsystem, not a
            # type. In a kernel tree that is the normal shape ('mm:', 'bpf:'),
            # so --kernel-sections claims it; otherwise map it explicitly and
            # keep the prefix as the scope so the sentence does not repeat it.
            raw = m.group("type")
            if kernel_sections and SUBSYS.match(subject):
                return {**c, "type": "kernel", "scope": raw, "subject": m.group("subject"),
                        "breaking": "BREAKING CHANGE" in c["body"].upper()}
            t = legacy.get(raw) or legacy.get(raw.lower()) or fallback
            scope, subject = raw, m.group("subject")
    else:
        # Subsystem prefix first: 'bpf, sockmap:' and 'mm/page_alloc.c:' are not
        # 'word:' shapes, so PREFIX alone would miss them.
        ksub = SUBSYS.match(subject)
        if kernel_sections and ksub:
            return {**c, "type": "kernel", "scope": ksub.group(1),
                    "subject": subject[ksub.end():],
                    "breaking": "BREAKING CHANGE" in c["body"].upper()}
        p = PREFIX.match(subject)
        raw = p.group(1) if p else ""
        t = legacy.get(raw) or legacy.get(raw.lower()) or fallback
        scope = raw or None
        subject = p.group(2) if p else c["subject"]
    breaking = bool(m and m.group("bang")) or "BREAKING CHANGE" in c["body"].upper()
    # **c first: it carries the raw subject, and dict-literal order decides the winner
    return {**c, "type": t, "scope": scope, "subject": subject.strip(), "breaking": breaking}


def render(commits: list[dict], version: str, date: str, repo: str | None,
           prev: str | None, exclude: set[str], contributors: bool = True) -> str:
    merged = [c for c in commits if c["type"] not in exclude]
    groups: dict[str, list[dict]] = {}
    for c in sorted(merged, key=lambda c: (ORDER.get(c["type"], 99), c["date"], c["short"])):
        groups.setdefault(c["type"], []).append(c)

    lines = [f"## {version}", ""]
    meta = []
    if date:
        meta.append(f"**{date}**")
    if repo and prev:
        meta.append(f"[compare changes](https://github.com/{repo}/compare/{prev}...{version})")
    if meta:
        lines += [" · ".join(meta), ""]

    for t, header in TYPES:
        rows = groups.get(t)
        if not rows:
            continue
        lines += [f"### {header}", ""]
        for c in rows:
            icon = ""
            if c["scope"]:
                icon = SCOPE_ICONS.get(c["scope"].lower(), SCOPE_ICON_FALLBACK) + " "
            label = f"{icon}**{c['scope']}:** " if c["scope"] else ""
            ref = (f"([`{c['short']}`](https://github.com/{repo}/commit/{c['sha']}))"
                   if repo else f"(`{c['short']}`)")
            lines.append(f"- {label}{c['subject']} {ref}")
        lines.append("")

    breaking = [c for c in merged if c["breaking"]]
    if breaking:
        lines += ["#### ⚠️ Breaking Changes", ""]
        for c in breaking:
            ref = f"([`{c['short']}`](https://github.com/{repo}/commit/{c['sha']}))" if repo else f"(`{c['short']}`)"
            lines.append(f"- ⚠️  {c['subject']} {ref}")
        lines.append("")

    if contributors:
        authors: list[str] = []
        for c in merged:
            a = c.get("author")
            if a and a not in authors:
                authors.append(a)
        if authors:
            lines += ["### ❤️ Contributors", ""]
            lines += [f"- {a}" for a in authors]
            lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--git", default=".")
    ap.add_argument("--from", dest="frm", help="tag/sha to start after")
    ap.add_argument("--to", default="HEAD")
    ap.add_argument("--version", help="heading, e.g. v1.0.0 (default: --to)")
    ap.add_argument("--date", default="", help="date line (leave empty for none)")
    ap.add_argument("--repo", help="owner/name, adds compare + commit links")
    ap.add_argument("--exclude", default="", help="comma list of types to drop")
    ap.add_argument("--legacy-map", default="", help="prefix=type pairs for non-conventional history")
    ap.add_argument("--fallback", default="other", help="type for unmappable commits ('skip' drops them)")
    ap.add_argument("--drop-reverts", action="store_true",
                    help="omit Revert/Reapply commits (a revert+reapply pair cancels out)")
    ap.add_argument("--kernel-sections", action="store_true",
                    help="treat 'mm: ...', 'ext4: ...' prefixes as kernel subsystems, not types")
    ap.add_argument("--exclude-authors", default=r"\[bot\]$",
                    help="regex of author names to drop (default: bot accounts)")
    ap.add_argument("--keep-skip-ci", action="store_true",
                    help="keep commits whose subject carries [skip ci] (dropped by default)")
    ap.add_argument("--no-contributors", action="store_true", help="drop the Contributors section")
    ap.add_argument("--out", default="-")
    a = ap.parse_args()

    legacy = {}
    for pair in filter(None, (p.strip() for p in a.legacy_map.split(","))):
        k, _, v = pair.partition("=")
        legacy[k.strip()] = v.strip() or a.fallback
    fallback = "chore" if a.fallback == "skip" else a.fallback

    rng = [f"{a.frm}..{a.to}"] if a.frm else []
    raw = log(a.git, rng)
    commits = [classify(c, legacy, fallback, a.kernel_sections) for c in raw]
    drops = []
    if not a.keep_skip_ci:
        drops += [c for c in commits if "[skip ci]" in c["subject"].lower()]
        commits = [c for c in commits if "[skip ci]" not in c["subject"].lower()]
    if a.exclude_authors:
        pat = re.compile(a.exclude_authors)
        drops += [c for c in commits if pat.search(c.get("author") or "")]
        commits = [c for c in commits if not pat.search(c.get("author") or "")]
    if drops:
        print(f"noise: dropped {len(drops)} commits (bot / [skip ci])", file=sys.stderr)
    if a.drop_reverts:
        dropped = [c for c in commits if c["type"] == "revert"]
        commits = [c for c in commits if c["type"] != "revert"]
        print(f"reverts: dropped {len(dropped)} Revert/Reapply commits", file=sys.stderr)
    if a.fallback == "skip":
        commits = [c for c in commits if c["type"] != fallback]

    md = render(commits, a.version or a.to, a.date, a.repo, a.frm,
                set(filter(None, a.exclude.split(","))), not a.no_contributors)
    if a.out == "-":
        sys.stdout.write(md)
    else:
        open(a.out, "w", encoding="utf-8").write(md)

    counts: dict[str, int] = {}
    for c in commits:
        counts[c["type"]] = counts.get(c["type"], 0) + 1
    print("summary: " + ", ".join(f"{t}={counts[t]}" for t, _ in TYPES if t in counts), file=sys.stderr)
    unmapped = [c["scope"] or c["subject"].split(":")[0][:16] for c in commits if c["type"] == fallback]
    if unmapped:
        uniq = sorted(set(unmapped))
        print(f"warning: {len(unmapped)} commits fell to '{fallback}' from {len(uniq)} prefixes: "
              f"{', '.join(uniq[:10])}", file=sys.stderr)


if __name__ == "__main__":
    main()
