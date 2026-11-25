#!/usr/bin/env python3
"""Check that libxrdome-core stays free of the Linux front end's headers.

Usage: tools/check_core.py LIST SRCROOT

LIST is src/core_sources.txt: one path per line, relative to SRCROOT (the
repository root); blank lines and lines starting with '#' are ignored. Exits 1,
naming the file, when a listed file is missing, or when it or any src/ header it
includes (recursively, through #include "...") includes an mpv/, SDL, X11/,
GL/glx or epoxy/glx header. A quoted include is looked up next to the including
file first, then under SRCROOT/src (the build's include directory); one found in
neither is a system header and not followed.
"""
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
FORBIDDEN = re.compile(r"^(mpv/|SDL|X11/|GL/glx|epoxy/glx)")


def read_list(list_path):
    """The listed paths, in order."""
    out = []
    for line in Path(list_path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.append(line)
    return out


def find_header(name, including, src_dir):
    """The src/ header a quoted include names, or None for a system header."""
    for base in (including.parent, src_dir):
        p = base / name
        if p.is_file():
            return p.resolve()
    return None


def problems(list_path, root):
    """One message per problem, each naming the file."""
    root = Path(root).resolve()
    src_dir = root / "src"
    msgs = []
    for rel in read_list(list_path):
        path = root / rel
        if not path.is_file():
            msgs.append(f"{rel}: listed in {list_path} but missing")
            continue
        seen = set()
        stack = [(path.resolve(), rel)]
        while stack:
            cur, chain = stack.pop()
            if cur in seen:
                continue
            seen.add(cur)
            text = cur.read_text(encoding="utf-8", errors="replace")
            for n, line in enumerate(text.splitlines(), 1):
                m = INCLUDE.match(line)
                if not m:
                    continue
                kind, name = m.groups()
                if FORBIDDEN.match(name):
                    where = cur.relative_to(root) if cur.is_relative_to(root) else cur
                    close = ">" if kind == "<" else '"'
                    msg = (f"{where.as_posix()}:{n}: includes {kind}{name}{close},"
                           " not allowed in the core")
                    if chain != rel:
                        msg += f" (reached as {chain})"
                    msgs.append(msg)
                elif kind == '"':
                    hdr = find_header(name, cur, src_dir)
                    if hdr is not None:
                        shown = hdr.relative_to(root) if hdr.is_relative_to(root) else hdr
                        stack.append((hdr, f"{chain} -> {shown.as_posix()}"))
    return msgs


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    msgs = problems(argv[1], argv[2])
    for m in msgs:
        print(f"check_core: {m}", file=sys.stderr)
    if msgs:
        return 1
    print(f"check_core: {len(read_list(argv[1]))} core files clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
