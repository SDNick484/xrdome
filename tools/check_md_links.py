#!/usr/bin/env python3
"""Fail on a dead relative link or #anchor in README.md and docs/*.md.

Usage: tools/check_md_links.py [FILE.md ...]   (default: README.md and docs/*.md)

Web links (http:, https:, mailto:) are not fetched. Anchors are GitHub's heading slugs:
lower case, punctuation other than '-' and '_' dropped, spaces to '-', and '-1', '-2' ...
for a repeated heading. Links inside code blocks, code spans and HTML comments are not links.
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def strip_code(text):
    """The text with fenced blocks, code spans and HTML comments blanked, line numbers kept."""
    text = re.sub(r"<!--.*?-->", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    out, fence = [], False
    for line in text.split("\n"):
        if line.lstrip().startswith("```"):
            fence = not fence
            out.append("")
            continue
        out.append("" if fence else re.sub(r"`[^`]*`", "", line))
    return out


def anchors(path):
    seen, out, fence = {}, set(), False
    for line in path.read_text(encoding="utf-8").split("\n"):
        if line.lstrip().startswith("```"):
            fence = not fence
            continue
        m = None if fence else re.match(r"#{1,6} +(.*?) *#*$", line)
        if not m:
            continue
        s = re.sub(r"[^\w\- ]", "", m.group(1).strip().lower()).replace(" ", "-")
        n = seen.get(s, 0)
        seen[s] = n + 1
        out.add(s if n == 0 else f"{s}-{n}")
    return out


def check(path):
    bad = []
    for no, line in enumerate(strip_code(path.read_text(encoding="utf-8")), 1):
        for m in LINK.finditer(line):
            target = m.group(1)
            if re.match(r"[a-z][a-z0-9+.-]*:", target):
                continue
            file, _, frag = target.partition("#")
            dest = (path.parent / file).resolve() if file else path
            if not dest.exists():
                bad.append(f"{os.path.relpath(path, ROOT)}:{no}: {target}: no such file")
            elif frag and dest.suffix == ".md" and frag not in anchors(dest):
                bad.append(f"{os.path.relpath(path, ROOT)}:{no}: {target}: no such anchor")
    return bad


def main(argv):
    files = [Path(a).resolve() for a in argv] or [ROOT / "README.md", *sorted((ROOT / "docs").glob("*.md"))]
    bad = [b for f in files for b in check(f)]
    for b in bad:
        print(b)
    print(f"{len(files)} files, {len(bad)} dead links")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
