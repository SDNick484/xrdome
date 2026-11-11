# xrdome: VR180 player for XR glasses on Linux (design spec)

Date: 2026-11-11
Status: approved
Repository: https://github.com/SDNick484/xrdome

## 1. Goal

Play VR180 video (side-by-side half-equirectangular) on XR glasses under Linux, with 3DoF head tracking, comfortable for seated viewing.

Success for the first usable version:

- A local 4K-class SBS VR180 file (up to 4096x2048, H.264 or HEVC, up to 60 fps) plays in correct stereo on the glasses.
- Looking around is smooth with no visible judder.
- A recenter key exists. Slow yaw drift (a few degrees per minute) is acceptable.
- Nothing in the design blocks publishing the project as open source later.

## 2. Scope

In scope for this spec:

- Viture Pro XR (primary) and XREAL Air 2 Pro (secondary).
- VR180 half-equirectangular, SBS left-right, right-left, and mono.
- 3DoF rotation only.
- Xorg. No Wayland requirement.

Out of scope for this spec (later work):

- 360 video, fisheye VR180, cubemap/EAC, top-bottom layouts.
- Format auto-detection from metadata or filenames.
- Automatic 2D/SBS display mode switching.
- A GTK launcher, a per-eye HUD, packaging in GURU.
- 8K playback as a requirement. It is a stretch goal to measure.
- Headset-grade latency (pose prediction, reprojection).

## 3. Environment

- Lenovo ThinkPad P14s Gen 6 AMD (Ryzen AI 7 350), integrated Radeon, Mesa radeonsi.
- Gentoo ~amd64, OpenRC, no systemd. MATE on Xorg.
- Glasses appear as an external monitor over DisplayPort alt mode.
- Modes confirmed working by the owner: 1920x1080 at 120 Hz (2D) and 3840x1080 at 60 Hz (SBS).
- XRLinuxDriver is not installed yet.

## 4. Research findings

Verified by reading XRLinuxDriver source at commit `da303d2` (2026-11-05) and the Viture headers it bundles, or by the cited pages.

### 4.1 Viture SDK

- SDK 1.0.7 (2024-06-19) added Viture Pro XR support. A newer C SDK (copyright 2025, `xr_device_provider_*` API) supports Linux x86_64 and aarch64 and lists the Pro as 3DoF.
- Headers: `viture_glasses_provider.h` (lifecycle), `viture_device.h` (IMU), `viture_protocol_public.h` (display mode, brightness).
- `VITURE_IMU_MODE_POSE` delivers `[roll, pitch, yaw, qw, qx, qy, qz]` in North-West-Up coordinates, fused on the device side of the SDK.
- `VITURE_IMU_MODE_RAW` delivers gyro and accel. The magnetometer slots are zero for One, Pro and Lite.
- Report rates: 60, 90, 120, 240, 500, 1000 Hz. XRLinuxDriver requests the highest the SDK reports as supported and defaults to 240 Hz.
- Display control exists: `xr_device_provider_set_display_mode(handle, VITURE_DISPLAY_MODE_3840_1080_60HZ)`.
- `libglasses.so` contains `libusb_detach_kernel_driver` and `libusb_claim_interface`, so only one process can own the glasses at a time.

### 4.2 XRLinuxDriver

- License GPL-3.0. Supports the Viture Pro XR (closed SDK) and XREAL Air 2 Pro (open-source driver, host-side fusion at 1000 Hz, 15 s calibration wait, README notes drift).
- Three ways to read pose from it:
  1. UDP in OpenTrack format: 6 doubles `(x, y, z, yaw, pitch, roll)` plus a uint32 frame number, 52 bytes, angles in degrees, default `127.0.0.1:4242`. Enabled with `xr_driver_cli --opentrack-app`. No license gate found in `opentrack_source.c`.
  2. SysV shared memory: 16 floats keyed by `ftok("/tmp/shader_runtime_pose_orientation", 0)` with a process-shared mutex (`src/ipc.c`).
  3. POSIX shm `/dev/shm/breezy_desktop_imu`: written only when `is_productivity_granted()` is true (paid tier). Not used.
- `bin/setup` requires systemd as PID 1 unless `SKIP_SYSTEMD` is set. udev rules use `TAG+="uaccess"`, which needs elogind.
- FOV table values: Viture Pro 43.0, XREAL Air 2 Pro 45.0.

### 4.3 Monado

- In-tree `xreal_air` driver covers Air, Air 2 and Air 2 Pro (PID `0x0432`) as 3DoF.
- No Viture driver or merge request found.
- An OpenXR runtime still needs an OpenXR VR180 player. None was confirmed for Linux.

### 4.4 Not verified

- Latency of the Viture pose path. No figure is published.
- Which config enables the SysV shared memory writer in XRLinuxDriver.
- Whether XRLinuxDriver's `xrDriver` binary runs cleanly on Gentoo/OpenRC.
- Rotation order of the Euler angles in the UDP packet.
- Whether the FOV table values are diagonal (assumed).
- That mpv's VA-API interop requires EGL rather than GLX on the installed mpv version (from memory).
- HEVC 4K60 hardware decode on this GPU (check with `vainfo`).
- Redistribution terms of the Viture SDK. None were found.
- EDID vendor and product strings for either pair of glasses.
- Gentoo USE flag names for mpv and SDL2 (confirm with `equery uses`).

## 5. Decisions

| Decision | Choice | Reason |
|---|---|---|
| Architecture | Custom renderer: libmpv plus an OpenGL shader pass | Per-frame pose sampling, full control of stereo and filtering, one codebase |
| Rejected: mpv + v360 over IPC | Throwaway at most | CPU filter, queued frames, poor latency |
| Rejected for now: Monado/OpenXR | Possible later backend | Months of work, no Viture driver, still needs a player |
| Pose source | Pluggable interface; first backend is XRLinuxDriver UDP | Works for both glasses, no linking against GPL or closed code |
| Viture SDK | Optional backend loaded with `dlopen` from a user-supplied path | Unknown license; never shipped, never linked |
| Language and build | C11, meson | libmpv, OpenGL and the Viture SDK are C APIs |
| Graphics API | OpenGL 3.3 core on EGL | libmpv render API target; EGL for VA-API interop |
| Window and input | SDL2, forced EGL on X11 | Fullscreen on a chosen output, keyboard, gamepad |
| GTK | Separate launcher later, not inside the player | Keeps toolkit frame clock out of the latency path |
| Decode | libmpv with `hwdec=vaapi` | Demux, A/V sync, seeking and audio for free |
| Display mode switching | Manual in v1 | Already works by hand; removes a device dependency |
| Name | `xrdome`, C prefix `xrd_` | `xrview` is heavily used elsewhere |

## 6. Architecture

One process. The main thread owns the GL context and the render loop. Each pose backend runs its own thread and publishes the latest sample through a seqlock, so the render loop never blocks on I/O. mpv keeps its own decode and audio threads.

### 6.1 Layout

```
xrdome/
  meson.build
  src/main.c
  src/pose/pose.h
  src/pose/pose_null.c
  src/pose/pose_udp_opentrack.c
  src/pose/pose_viture_sdk.c      (later, optional)
  src/pose/pose_xreal_hid.c       (later)
  src/track/tracker.c
  src/video/player_mpv.c
  src/render/renderer_gl.c
  src/render/shaders/vr180.frag
  src/display/output.c
  tests/
```

### 6.2 Pose interface

```c
typedef struct { float w, x, y, z; } xrd_quat;   /* world-from-head, GL axes:
                                                    +x right, +y up, -z forward */
typedef struct {
    xrd_quat q;
    uint64_t t_mono_ns;   /* CLOCK_MONOTONIC at receipt */
    uint32_t seq;
} xrd_pose;

typedef struct xrd_pose_source {
    const char *name;
    int  (*open)(struct xrd_pose_source *, const char *args);
    int  (*latest)(struct xrd_pose_source *, xrd_pose *out);  /* non-blocking */
    void (*close)(struct xrd_pose_source *);
} xrd_pose_source;
```

Rules:

- Each backend converts to the single GL-axis quaternion convention. Nothing above the interface knows about NWU or Euler angles.
- Recentering lives above the backends: `q_view = conj(q_ref) * q_raw`. A yaw-only mode keeps the horizon level.
- Selection by flag: `xrdome --pose udp-opentrack:port=4242 file.mp4`.
- With `--pose null` the orientation is fixed, and dragging with the left mouse button looks around, for testing without glasses.

Known limitation: the UDP backend receives Euler angles and degrades near plus or minus 90 degrees pitch. Acceptable for seated VR180. The SDK backend removes it.

### 6.3 Video and render path

```
file -> mpv demux/decode (VA-API) -> mpv renders RGB into an FBO texture (video-sized)
     -> eye pass: one fullscreen draw per eye, sampling the texture with the latest pose
     -> swap at 60 Hz on the 3840x1080 output
```

- The loop runs at display rate. Each vsync it calls `mpv_render_context_update()`; if a new frame is due, mpv draws it into the FBO. The eye pass is always redrawn with a fresh pose, so a 30 fps file still tracks at 60 Hz.
- libmpv setup: options `vo=libmpv`, `hwdec=vaapi`, `osd-level=0`. `mpv_render_context_create()` with `MPV_RENDER_API_TYPE_OPENGL`, a `get_proc_address` callback and `MPV_RENDER_PARAM_X11_DISPLAY`. `mpv_render_context_render()` with `MPV_RENDER_PARAM_OPENGL_FBO` sized to the video.
- The FBO is the video's native size so mpv does no scaling.
- The player reads mpv's `hwdec-current` property after the first frame and warns if it is not `vaapi`.
- Eye pass: two 1920x1080 viewports. Same rotation for both eyes. No IPD offset, because parallax is baked into the footage.

Shader core:

```glsl
uniform sampler2D u_video;
uniform mat3  u_rot;        // from q_view
uniform vec2  u_tan_half;   // tan(hfov/2), tan(vfov/2)
uniform float u_eye;        // 0.0 left, 1.0 right
in  vec2 v_ndc;             // -1..1 within this eye's viewport
out vec4 o_color;

void main() {
    vec3 d = u_rot * normalize(vec3(v_ndc * u_tan_half, -1.0));
    float lon = atan(d.x, -d.z);
    float lat = asin(clamp(d.y, -1.0, 1.0));
    if (abs(lon) > 1.5707963) { o_color = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec2 uv = vec2(lon / 3.14159265 + 0.5, lat / 3.14159265 + 0.5);
    uv.x = (clamp(uv.x, 0.0005, 0.9995) + u_eye) * 0.5;
    o_color = texture(u_video, uv);
}
```

FOV, derived from the diagonal figures assuming 16:9:

| Glasses | Diagonal | Horizontal | Vertical |
|---|---|---|---|
| Viture Pro XR | 43.0 | 37.9 | 21.9 |
| XREAL Air 2 Pro | 45.0 | 39.7 | 23.0 |

A zoom multiplier scales the effective FOV, because the 1:1 mapping shows a narrow window of the 180 degree frame.

Filtering: bilinear in v1. The video is magnified about 4.5x (about 51 display px/degree against about 11 source px/degree for a 4096x2048 file), so there is no minification and no mipmaps. Bicubic is a later option.

### 6.4 Display and UX

- `output.c` picks the output whose current mode is 3840x1080. `--output NAME` or config overrides it.
- v1 does not switch display modes. If the chosen output is not 3840x1080, the player exits with a message saying to switch the glasses to SBS.
- Window: SDL2 with `SDL_HINT_VIDEO_X11_FORCE_EGL=1`, borderless, placed on the glasses' output geometry. The laptop panel keeps the desktop.
- mpv's OSD is disabled, since it would be drawn once across the SBS texture and split between the eyes.

Keys:

| Key | Action |
|---|---|
| Space | Pause |
| Left / Right | Seek 10 s |
| R | Recenter, yaw-only |
| Shift+R | Recenter, full |
| [ / ] | Zoom out / in |
| S | Swap eyes |
| M | Cycle layout: sbs-lr, sbs-rl, mono |
| Q | Quit |

Flags: `--proj hequirect`, `--layout sbs-lr|sbs-rl|mono`, `--pose SPEC`, `--output NAME`, `--device NAME`, `--zoom F`, `--require-hwdec`, `--windowed`, `--timing-log PATH`.

Config at `~/.config/xrdome/config.ini`:

```ini
[general]
pose = udp-opentrack:port=4242
output = auto

[device.viture-pro]
fov_diag = 43.0
zoom = 1.0

[device.xreal-air2pro]
fov_diag = 45.0
zoom = 1.0
```

Dependencies (Gentoo atoms, flags to confirm): `media-video/mpv[libmpv,vaapi]`, `media-libs/libsdl2[X,opengl]`, `media-libs/libepoxy`, `dev-build/meson`, `dev-util/cmocka` for tests.

### 6.5 Error handling

The render loop never blocks and never crashes on tracking problems.

| Condition | Behavior |
|---|---|
| No pose data at startup | Play with identity orientation, log a warning once |
| Pose stale (no sample for 250 ms) | Hold last orientation, log once, resume when samples return |
| UDP packet not exactly 52 bytes | Drop and count; print the count at exit |
| Output is not 3840x1080 | Exit non-zero with a message to switch the glasses to SBS |
| Glasses unplugged mid-play | Pause, then exit cleanly |
| `hwdec-current` is not `vaapi` | Warn and continue; with `--require-hwdec`, exit non-zero |
| File missing or undecodable | Exit non-zero with mpv's error string |
| Viture backend: `dlopen` fails | Exit naming the path tried |
| Viture backend: USB interface busy | Exit saying another process holds the glasses |

## 7. Testing

Unit tests with cmocka under `meson test`, no hardware needed:

- Quaternion math: Euler-to-quaternion for the UDP convention, recenter, yaw-only extraction.
- Packet parser: valid 52-byte packet, short and long packets, NaN angles.
- Projection math: the shader's direction-to-UV mapping mirrored in C. Straight ahead maps to (0.5, 0.5); 90 degrees right maps to u = 1.0; anything behind is "outside".
- FOV derivation: 43 degrees diagonal gives 37.9 by 21.9.
- Config parser: defaults, overrides, bad values.

Render test, no hardware needed: an offscreen EGL surfaceless context on Mesa llvmpipe renders a synthetic SBS pattern (left half red, right half blue) and asserts pixel colors at known points. Catches eye swap, flipped axes and seam bleed.

Hardware checks, manual, one per plan phase:

- Packets arrive on port 4242 and angles change with head movement.
- Head right moves the image left; head up moves it down.
- With the red/blue pattern, closing the right eye leaves red.
- Frame timing log shows no swap interval over 20 ms for 5 minutes.

Performance targets:

- Zero missed vsyncs over 5 minutes of 4K60 HEVC.
- Pose sample to swap call under 4 ms, logged per frame from `CLOCK_MONOTONIC`.
- `hwdec-current` is `vaapi` and the process stays under one CPU core.

Not tested: end-to-end motion-to-photon latency. It needs a high-speed camera and the tracking bar does not justify it.

## 8. Risks, ranked

1. Getting orientation at all on Gentoo/OpenRC. Upstream assumes systemd and prebuilt binaries.
2. Source resolution. 4K-class VR180 will look soft at about 4.5x magnification regardless of renderer.
3. Yaw drift on both glasses, worse on XREAL.
4. Viture SDK license is unknown.
5. Pose path latency is unmeasured.

## 9. Phase outline

The detailed plan is `docs/superpowers/plans/2026-11-11-xrdome.md`. Order, cheapest experiment against the biggest risk first:

0. Pose feasibility: run `xrDriver` by hand on OpenRC, enable the OpenTrack output, confirm packets with `tcpdump`. Fallback: a small C probe against `libglasses.so`.
1. Fixed-orientation playback: SDL2 and EGL window on the glasses, libmpv into an FBO, eye pass with identity rotation.
2. Head tracking: pose interface, UDP backend, recenter.
3. Robustness: error table, config file, keys, timing log.
4. XREAL Air 2 Pro verification.
5. Later: Viture SDK backend, format auto-detection, HUD, GTK launcher, overlay ebuild.

## 10. Sources

- https://www.viture.com/blog/developer/viture-xr-glasses-sdk-for-linux
- https://www.viture.com/developer/glasses-sdk/glasses
- https://github.com/wheaney/XRLinuxDriver (source read at commit `da303d2`)
- https://xrlinuxdriver.readthedocs.io/en/latest/opentrack-app/
- https://gitlab.freedesktop.org/monado/webpage/-/blob/master/index.md
- https://monado.pages.freedesktop.org/monado/group__drv__xreal__air.html
