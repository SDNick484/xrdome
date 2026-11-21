# Measurements

## Performance notes

Measured 2026-11-12 on a ThinkPad P14s Gen 6 AMD (integrated Radeon, Mesa, Xorg/MATE) driving
Viture Pro XR glasses at 3840x1080 60 Hz, with a 4320x2160 10-bit AV1 60 fps file, VA-API decode,
10-bit video texture and dithering on:

| Settings | Frames | Swap intervals over 20 ms |
|---|---|---|
| mpv default render path (before `--mpv-profile fast` became the default), 8-bit texture | 456 | 251 (55%) |
| Defaults: profile `fast`, bicubic (filter toggled during the run) | 15345 | 96 (0.6%) |
| `--filter bilinear` | 4547 | 29 (0.6%) |
| `--mpv-opt gpu-dumb-mode=yes` | 2482 | 13 (0.5%) |

- mpv's `fast` profile is what fixed the frame rate. Bicubic sampling and mpv's dumb mode make no
  measurable difference on top of it, so the defaults stay `fast` and `bicubic`.
- Median time from pose sample to swap call: 0.003 ms. The swap, not the draw, is where the loop
  waits for vsync on this driver, so `--sync-after-swap` is not needed here.
- What the remaining 0.5 to 0.6% of late frames are has not been analysed.
- Decode ceilings on this GPU: 7680x2880 AV1 at 47 fps, the same frame size in HEVC at 132 fps.
- H.264 hardware decode on this GPU is limited to 4096x4096, so a 4320x2160 H.264 file falls back
  to software decode (`hwdec: no`; mpv prints the reason). Software decode of that file at 60 fps
  and 20 Mbit/s still kept up: 446 of 34376 swap intervals (1.3%) exceeded 20 ms. HEVC and AV1 do
  not have this size limit here.
- That holds on AC power. On battery with the `balanced` platform profile (2026-11-13) the same
  kind of file missed 57 to 74% of swap intervals over 3600 frames; plugged in, 0 of 3600. If
  software-decoded playback stutters, check the charger first.
- Source bitrate matters more than anything the player does: the same resolution looked much
  better at 20 Mbit/s H.264 than at 2.5 Mbit/s AV1.
- Mipmaps (2026-11-14, AC power, 3600 frames per run, no keys): a 3840x2160 AV1 flat film at
  `--zoom 1.0` missed 8 vsyncs (0.22%) with `--mipmap off` and 13 (0.36%) with `on`. A 7680x3840
  AV1 VR180 file at `--zoom 0.5` missed 113 (3.1%) without mipmaps and 85 (2.4%) with
  `--mipmap-force`, which makes the chain every frame. One pair each; the differences are within
  run-to-run noise. A flat 1080p AV1 film missed 0 of 10754.

- Debanding (2026-11-14, AC power, 3600 frames per run, no keys): a 3840x2160 AV1 flat film at
  zoom 1.5 missed 1 vsync with `--deband off` and 27 (0.75%) with `on`; a 7680x3840 AV1 VR180 file
  at zoom 1.0 missed 59 (1.6%) and 1015 (28%). At 8K mpv's deband saturates the GPU.
- `--mpv-scale` (2026-11-14, same conditions), a 1080p AV1 film: at zoom 1.5 (texture 2880x1620)
  `off` 0 missed, `spline36` 2, `ewa_lanczossharp` 2; at zoom 2.0 (texture 3840x2160, the cap) 1, 1
  and 7. Cheap on this GPU.
- The 4K AV1 film's late frames fall on a spike every 32 display frames (16 source frames),
  present with and without deband; not explained.

## Tracking notes

Measured 2026-11-11, Viture Pro XR (`35ca:101d`), XRLinuxDriver 2.11.9, Gentoo/OpenRC:

- About 365 packets per second, no malformed packets.
- The packet carries the driver's NWU Euler angles: `q = Rz_up(yaw) * Ry_left(pitch) * Rx_forward(roll)`.
- The pose is absolute in the driver's own world frame. Looking level and forward read roughly
  yaw -157, pitch 12, roll -176, so raw angles are not usable directly.
- Rotation relative to a reference pose, `conj(q_ref) * q`, has standard NWU body axes:
  head left is positive about up, looking up is negative about left, right ear down is positive about forward.
- Yaw drift at rest (2026-11-14): 0.02 degrees in 5 minutes, glasses lying still, hours after
  they connected.

XREAL Air 2 Pro (`3318:0432`), same driver, 2026-11-14: about 1000 packets per second, no malformed
packets; head tracking works with `--device xreal-air2pro` much as on the Viture (owner; the axis
signs were not checked one by one).

- **Keep the XREAL still while the driver calibrates it**, about 16 seconds after it is plugged in
  (or the driver started), until its log says `Device calibration complete`. Calibrated while
  being handled or worn, its yaw slid 3 to 17 degrees over the next half minute, which shows in
  xrdome as the picture drifting sideways after a recenter; calibrated lying still, it moved 0.06
  degrees in a minute. Worn and still long after calibration it drifted 0.08 degrees per minute.
  The plan's Task 10 already asked for 15 still seconds; this is the measurement.
- Whether xrdome's automatic SBS switch (which restarts the glasses' IMU stream in the driver)
  has the same effect is not known: the picture drifted after it twice, six minutes after
  calibration.

Hardware-check notes for tracking:

- Look at `pose_to_swap_us` per frame, not only the max. If it often sits near 8-16 ms while
  `interval_us` is about 16.7 ms, re-run with `--sync-after-swap` and compare.
- Time how long the player takes to exit after the glasses are unplugged (XREAL Air 2 Pro,
  2026-11-14: about 0.3 s; the display goes before the USB device does).
- Use a file the machine can decode at full rate.

## Hardware notes

From `xrandr --props` and `edid-decode` on the owner's laptop, 2026-11-14 (full captures in
`docs/hardware/`):

| Glasses | USB ID | EDID vendor, product name | 1080p mode | SBS mode |
|---|---|---|---|---|
| Viture Pro XR | `35ca:101d` | `CVT`, `VITURE` | 1920x1080 at 60 (preferred), 90, 120 Hz | 3840x1080 at 60 Hz only |
| XREAL Air 2 Pro | `3318:0432` | `MRG`, `Air 2 Pro` | 1920x1080 at 60 (preferred), 90, 120 Hz | 3840x1080 at 60 Hz only |

- Both report the same placeholder EDID serial number (0x88888800), so the serial cannot tell
  two pairs apart.
- After a replug the glasses' output can come back under another name (`DisplayPort-1`, then
  `DisplayPort-0`); xrdome finds the glasses' output by its size, not its name.
- Each pair has a button that switches between 1080p and SBS: Viture, the small button at the
  front left, held 2 seconds; XREAL, volume up on the right arm, held 2 seconds.

Known limits:

- The driver sends Euler angles, so the pose is ambiguous near 90 degrees of pitch (looking
  straight up or down).
- Yaw can drift slowly over a long session; `R` recentres it. See
  [Tracking notes](#tracking-notes) for the XREAL's calibration.
- A 4K VR180 file has about 11 source pixels per degree against the glasses' 51, so it looks soft
  however it is scaled; see [Viewing comfort and sharpness](picture-quality.md#viewing-comfort-and-sharpness).
