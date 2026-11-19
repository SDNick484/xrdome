#!/usr/bin/env python3
"""xrdome-pose-viture as a program, against the fake SDK (fake_sdk.c).

usage: test_process.py HELPER FAKE_SDK

Covers what the in-process tests cannot reach: the exit codes and messages,
the packets as a plain UDP receiver sees them, the display mode put back
after --sbs, Ctrl+C (SIGINT) ending the session cleanly, and the rate cap.
"""
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

HELPER = FAKE = None


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Receiver:
    """A UDP socket that collects OpenTrack packets."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(5.0)
        self.port = self.sock.getsockname()[1]

    def packets(self, n):
        out = []
        while len(out) < n:
            data, _ = self.sock.recvfrom(256)
            out.append(data)
        return out

    def close(self):
        self.sock.close()


class Helper(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = os.path.join(self.tmp.name, "calls.log")
        self.driver_dir = os.path.join(self.tmp.name, "drv")
        os.mkdir(self.driver_dir)
        self.env = dict(os.environ)
        self.env["XRD_FAKE_VITURE_LOG"] = self.log
        # Never look at a real XRLinuxDriver's files.
        self.env["XRDOME_XR_DRIVER_DIR"] = self.driver_dir
        for k in ("XRD_FAKE_VITURE_MODE", "XRD_FAKE_VITURE_POSE", "XRD_FAKE_VITURE_HZ"):
            self.env.pop(k, None)

    def run_helper(self, *args, timeout=30):
        return subprocess.run([HELPER, *args], env=self.env, capture_output=True, text=True,
                              timeout=timeout)

    def popen(self, *args):
        return subprocess.Popen([HELPER, *args], env=self.env, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)

    def calls(self):
        try:
            with open(self.log) as f:
                return f.read().split("\n")[:-1]
        except FileNotFoundError:
            return []

    def sdk(self, *extra):
        return ["--lib", FAKE, "--port", str(self.port), *extra]

    # ---- usage and errors

    def test_help_and_version(self):
        r = self.run_helper("--help")
        self.assertEqual(r.returncode, 0)
        self.assertIn("--sbs", r.stdout)
        r = self.run_helper("--version")
        self.assertEqual(r.returncode, 0)
        self.assertTrue(r.stdout.startswith("xrdome-pose-viture "))

    def test_bad_option_exits_2(self):
        r = self.run_helper("--port", "99999")
        self.assertEqual(r.returncode, 2)
        self.assertIn("--port", r.stderr)
        r = self.run_helper("--nope")
        self.assertEqual(r.returncode, 2)

    def test_missing_library_is_named(self):
        missing = os.path.join(self.tmp.name, "no-such-libglasses")
        r = self.run_helper("--lib", missing)
        self.assertEqual(r.returncode, 1)
        self.assertIn(missing.replace("\\", "/"), r.stderr.replace("\\", "/"))
        self.assertEqual(self.calls(), [])

    @unittest.skipUnless(sys.platform.startswith("linux"), "XRLinuxDriver is Linux only")
    def test_refuses_while_xrlinuxdriver_runs(self):
        with open(os.path.join(self.driver_dir, "xr_driver_state"), "w") as f:
            f.write("heartbeat=%d\n" % int(time.time()))
        r = self.run_helper("--lib", FAKE, "--pid", "0x1019", "--count", "3")
        self.assertEqual(r.returncode, 1)
        self.assertIn("XRLinuxDriver", r.stderr)
        self.assertEqual(self.calls(), [])  # the SDK was not even loaded and called

    @unittest.skipUnless(sys.platform.startswith("linux"), "XRLinuxDriver is Linux only")
    def test_stale_xrlinuxdriver_state_is_ignored(self):
        with open(os.path.join(self.driver_dir, "xr_driver_state"), "w") as f:
            f.write("heartbeat=%d\n" % (int(time.time()) - 600))
        rx = Receiver()
        self.port = rx.port
        r = self.run_helper(*self.sdk("--pid", "0x1019", "--count", "3"))
        rx.close()
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_unknown_pid(self):
        r = self.run_helper("--lib", FAKE, "--pid", "0x4444")
        self.assertEqual(r.returncode, 1)
        self.assertIn("0x4444", r.stderr)

    # ---- packets

    def test_packets_carry_the_fake_pose(self):
        rx = Receiver()
        self.port = rx.port
        self.env["XRD_FAKE_VITURE_POSE"] = "-60,25,-15"
        p = self.popen(*self.sdk("--pid", "0x101D", "--count", "20"))
        pkts = rx.packets(20)
        out, err = p.communicate(timeout=20)
        rx.close()
        self.assertEqual(p.returncode, 0, err)
        frames = []
        for d in pkts:
            self.assertEqual(len(d), 52)
            x, y, z, yaw, pitch, roll, frame = struct.unpack("<6dI", d)
            self.assertEqual((x, y, z), (0.0, 0.0, 0.0))
            self.assertAlmostEqual(yaw, -60.0, places=3)
            self.assertAlmostEqual(pitch, 25.0, places=3)
            self.assertAlmostEqual(roll, -15.0, places=3)
            frames.append(frame)
        self.assertEqual(frames, list(range(20)))
        calls = self.calls()
        self.assertEqual(calls[0], "create 4125")
        self.assertEqual(calls[-4:], ["close_imu 1", "stop", "shutdown", "destroy"])

    def test_sbs_goes_to_3840_and_back(self):
        rx = Receiver()
        self.port = rx.port
        self.env["XRD_FAKE_VITURE_MODE"] = "0x33"  # 1920x1080 at 90 Hz
        r = self.run_helper(*self.sdk("--pid", "0x1019", "--sbs", "--count", "5"))
        rx.close()
        self.assertEqual(r.returncode, 0, r.stderr)
        calls = self.calls()
        self.assertIn("set_display_mode 50", calls)  # 0x32
        self.assertEqual(calls[-4:], ["set_display_mode 51", "stop", "shutdown", "destroy"])  # 0x33
        self.assertLess(calls.index("set_display_mode 50"), calls.index("open_imu 1 4"))

    def test_without_sbs_the_mode_is_never_touched(self):
        rx = Receiver()
        self.port = rx.port
        r = self.run_helper(*self.sdk("--pid", "0x1019", "--count", "5"))
        rx.close()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertFalse([c for c in self.calls() if "display_mode" in c])

    @unittest.skipIf(sys.platform == "win32", "no console Ctrl+C to send to a child here")
    def test_sigint_restores_the_display_mode(self):
        rx = Receiver()
        self.port = rx.port
        p = self.popen(*self.sdk("--pid", "0x1019", "--sbs"))
        rx.packets(5)  # it runs
        p.send_signal(signal.SIGINT)
        try:
            out, err = p.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            p.kill()
            self.fail("the helper did not end on SIGINT")
        rx.close()
        self.assertEqual(p.returncode, 0, err)
        calls = self.calls()
        self.assertEqual(calls[-4:], ["set_display_mode 49", "stop", "shutdown", "destroy"])  # 0x31
        self.assertIn("display mode back", err)

    def test_rate_cap(self):
        rx = Receiver()
        self.port = rx.port
        t0 = time.monotonic()
        p = self.popen(*self.sdk("--pid", "0x1019", "--rate", "50", "--count", "26"))
        rx.packets(26)
        took = time.monotonic() - t0
        p.communicate(timeout=20)
        rx.close()
        # 26 packets at no more than 50 a second take at least 0.5 s (less the
        # first, which goes at once); uncapped, the fake's 500 Hz takes 0.05 s.
        self.assertGreaterEqual(took, 0.45)


if __name__ == "__main__":
    HELPER, FAKE = sys.argv[1], sys.argv[2]
    del sys.argv[1:3]
    unittest.main(verbosity=2)
