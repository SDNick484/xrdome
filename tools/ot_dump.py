#!/usr/bin/env python3
"""Print XRLinuxDriver's OpenTrack UDP pose packets once a second."""
import socket, struct, time

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 4242))
n, bad, t0 = 0, 0, time.monotonic()
try:
    while True:
        data, _ = s.recvfrom(256)
        if len(data) != 52:
            bad += 1
            continue
        x, y, z, yaw, pitch, roll, frame = struct.unpack("<6dI", data)
        n += 1
        now = time.monotonic()
        if now - t0 >= 1.0:
            print(f"yaw {yaw:8.2f}  pitch {pitch:8.2f}  roll {roll:8.2f}  "
                  f"{n / (now - t0):6.1f} pkt/s  bad {bad}  frame {frame}")
            n, t0 = 0, now
except KeyboardInterrupt:
    pass
