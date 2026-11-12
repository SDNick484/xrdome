#!/usr/bin/env python3
"""Send synthetic OpenTrack UDP pose packets, standing in for XRLinuxDriver.

Default: yaw follows a sine between -40 and 40 degrees with a 4 s period,
pitch and roll 0, at 100 Hz to 127.0.0.1:4242, until interrupted.

--yaw DEG sends a constant yaw instead (pitch and roll 0). --count N stops
after N packets. --rate HZ sets the packet rate. Angles are the driver's NWU
convention: positive yaw is a turn to the LEFT.

Packet: little-endian <6dI, x y z yaw pitch roll (doubles), frame (uint32).
"""
import argparse
import math
import socket
import struct
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=4242)
    ap.add_argument("--rate", type=float, default=100.0, help="packets per second (default 100)")
    ap.add_argument("--yaw", type=float, default=None,
                    help="constant yaw in degrees instead of the sine sweep")
    ap.add_argument("--amplitude", type=float, default=40.0,
                    help="sine sweep amplitude in degrees (default 40)")
    ap.add_argument("--period", type=float, default=4.0,
                    help="sine sweep period in seconds (default 4)")
    ap.add_argument("--count", type=int, default=0, help="stop after N packets (0 = forever)")
    args = ap.parse_args()
    if args.rate <= 0:
        ap.error("--rate must be positive")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.host, args.port)
    step = 1.0 / args.rate
    t0 = time.monotonic()
    next_t = t0
    frame = 0
    try:
        while args.count == 0 or frame < args.count:
            now = time.monotonic()
            if args.yaw is not None:
                yaw = args.yaw
            else:
                yaw = args.amplitude * math.sin(2.0 * math.pi * (now - t0) / args.period)
            pkt = struct.pack("<6dI", 0.0, 0.0, 0.0, yaw, 0.0, 0.0, frame & 0xFFFFFFFF)
            try:
                sock.sendto(pkt, dest)
            except OSError:
                pass  # nobody listening yet (ICMP port unreachable); keep sending
            frame += 1
            next_t += step
            delay = next_t - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                next_t = time.monotonic()  # fell behind; do not burst
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
