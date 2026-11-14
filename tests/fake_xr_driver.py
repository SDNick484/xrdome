#!/usr/bin/env python3
"""Stands in for XRLinuxDriver's SBS switch in the Xvfb smoke test (tests/smoke_sbs.sh).

usage: fake_xr_driver.py DIR [--ignore]

Like the driver, it rewrites DIR/xr_driver_state with a fresh heartbeat, here
every 0.2 s, with glasses that support SBS and start in 1080p, and takes each
request written to DIR/xr_driver_control: appends the file's text to
DIR/requests.log and deletes the file. On sbs_mode=enable it grows the Xvfb
screen to one 3840x1080 output with xrandr (unless --ignore, which only logs)
and reports sbs_mode_enabled=true; on sbs_mode=disable it only reports
sbs_mode_enabled=false (xrdome has exited by then). DIR/ready appears after the first state
write; it exits when DIR/stop appears, or after 120 s.
"""
import os
import subprocess
import sys
import time


def write_state(d, enabled):
    tmp = os.path.join(d, "xr_driver_state.tmp")
    with open(tmp, "w") as f:
        f.write(f"heartbeat={int(time.time())}\n"
                "connected_device_brand=VITURE\n"
                "connected_device_model=Pro\n"
                "sbs_mode_supported=true\n"
                f"sbs_mode_enabled={'true' if enabled else 'false'}\n")
    os.replace(tmp, os.path.join(d, "xr_driver_state"))


def main():
    d = sys.argv[1]
    ignore = "--ignore" in sys.argv[2:]
    control = os.path.join(d, "xr_driver_control")
    enabled = False
    write_state(d, enabled)
    open(os.path.join(d, "ready"), "w").close()
    last = time.monotonic()
    end = last + 120
    while not os.path.exists(os.path.join(d, "stop")) and time.monotonic() < end:
        try:
            with open(control) as f:
                text = f.read()
        except FileNotFoundError:
            text = None
        # The writer truncates, writes and closes; act only on a whole line.
        if text and text.endswith("\n"):
            with open(os.path.join(d, "requests.log"), "a") as log:
                log.write(text)
            os.remove(control)
            if "sbs_mode=enable\n" in text.splitlines(keepends=True) and not ignore:
                subprocess.run(["xrandr", "--fb", "3840x1080", "--output", "screen",
                                "--mode", "3840x1080"], check=True)
                enabled = True
            elif "sbs_mode=disable\n" in text.splitlines(keepends=True):
                enabled = False
            write_state(d, enabled)
        if time.monotonic() - last >= 0.2:
            write_state(d, enabled)
            last = time.monotonic()
        time.sleep(0.02)


if __name__ == "__main__":
    main()
