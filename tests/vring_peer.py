#!/usr/bin/env python3
"""The helper's side of a ring for tests/test_framesink.c: tools/xrdome_depth/video.py against the C ring.

    vring_peer.py TOOLS_DIR SHM_NAME FRAME

Maps the ring the C test made (and removes its name, as `depth serve` does), checks that the newest frame
is FRAME with the test's pattern (pixel x, y = x % 256, y % 256, FRAME, 255) and its time 1234, and writes
depth map 1 made from it: sample x, y = (y * 322 + x) % 65536. Exits non-zero, saying why, otherwise."""

import array
import sys

sys.path.insert(0, sys.argv[1])
from xrdome_depth import video  # noqa: E402

ring, mm = video.open_shm(sys.argv[2])
want = int(sys.argv[3])
got = ring.frame_read(0)
assert got is not None, "no frame"
n, t_ns, data = got
assert (n, t_ns) == (want, 1234), (n, t_ns)
for y in (0, 1, 90, 181):
    for x in (0, 1, 200, 321):
        off = (y * video.WIDTH + x) * 4
        assert data[off:off + 4] == bytes([x % 256, y % 256, want, 255]), (x, y, data[off:off + 4])
samples = array.array("H", (i % 65536 for i in range(video.WIDTH * video.HEIGHT)))
ring.depth_write(1, n, samples.tobytes())
ring.release()
mm.close()
