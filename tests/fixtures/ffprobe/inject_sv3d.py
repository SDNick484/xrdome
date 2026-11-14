#!/usr/bin/env python3
"""Add an sv3d box (equirectangular, the Spherical Video V2 layout) to the first
video sample entry of an MP4 whose moov box comes after mdat (ffmpeg's default).
Usage: inject_sv3d.py IN.mp4 OUT.mp4 [equi|cbmp]
Only used to make test fixtures; see README."""
import struct, sys

def box(t, body):
    return struct.pack(">I4s", 8 + len(body), t) + body

def main():
    src, dst = sys.argv[1], sys.argv[2]
    kind = sys.argv[3] if len(sys.argv) > 3 else "equi"
    d = bytearray(open(src, "rb").read())
    if kind == "equi":
        proj_body = box(b"equi", struct.pack(">IIIII", 0, 0, 0, 0, 0))
    else:  # cbmp: version/flags, layout, padding
        proj_body = box(b"cbmp", struct.pack(">III", 0, 0, 0))
    prhd = box(b"prhd", struct.pack(">Iiii", 0, 0, 0, 0))
    svhd = box(b"svhd", struct.pack(">I", 0) + b"xrdome fixture\0")
    sv3d = box(b"sv3d", svhd + box(b"proj", prhd + proj_body))

    def find(t, lo, hi):
        i = d.find(t, lo, hi)
        if i < 4:
            sys.exit("box %r not found" % t)
        return i - 4  # box start

    moov = find(b"moov", 0, len(d))
    mdat = d.find(b"mdat")
    if not (0 <= mdat < moov):
        sys.exit("moov must come after mdat")
    chain = [moov]
    for t in (b"trak", b"mdia", b"minf", b"stbl", b"stsd"):
        chain.append(find(t, chain[-1] + 8, len(d)))
    # stsd: 8 header + 4 version/flags + 4 entry count, then the sample entry
    entry = chain[-1] + 16
    entry_size = struct.unpack(">I", d[entry:entry + 4])[0]
    insert_at = entry + entry_size
    d[insert_at:insert_at] = sv3d
    for start in chain + [entry]:
        size = struct.unpack(">I", d[start:start + 4])[0]
        d[start:start + 4] = struct.pack(">I", size + len(sv3d))
    open(dst, "wb").write(d)

main()
