# Licence

GPL-2.0-or-later, the same terms as mpv built with its GPL files (see `Copyright` in the
[mpv repository](https://github.com/mpv-player/mpv)); xrdome links libmpv. The full text is in
`LICENSE`. The sharpening filter in `src/render/shaders/sharpen.frag` is a port of AMD FidelityFX
CAS and stays under AMD's MIT licence, whose notice is in that file (and in the shader text
compiled into the program); MIT is compatible with the GPL. `helpers/xrdome-pose-viture/` is a separate
program under the MIT licence (its own `LICENSE`); it shares no source with xrdome and never contains
VITURE's SDK, which is proprietary and is loaded from the user's own copy.

The VITURE Pro XR's USB protocol in `src/pose/viture_proto.c` and the `viture-usb` pose backend in
`src/pose/pose_viture_usb.c` are ported from `viture_connection.c` of Michael Gschwandtner's
[viture_virtual_display](https://github.com/mgschwan/viture_virtual_display) (MIT; its notice is in
both files). That file says it reimplements the protocol from a **decompiled VITURE SDK**: xrdome
contains none of VITURE's code or headers, but the protocol knowledge comes from that decompilation.
The project accepts this provenance for now; if VITURE asks for the code's removal, it is removed and
replaced by option B of the Android design (`docs/superpowers/specs/2026-11-25-android-vr180-design.md`,
section 4.1): a separate MIT companion app holding VITURE's own Android SDK and sending the pose to
xrdome as OpenTrack UDP. The Viture code is kept in its own files, which nothing else depends on, so
that switch is cheap. Not legal advice.

## The Android app

The Android APK (`android/`, built by the `android` CI workflow) is GPL-3.0-or-later as a whole. It
contains the xrdome core, which is GPL-2.0-or-later ("or later" lets it combine with Apache-2.0
code under GPLv3), and AndroidX Media3, which is Apache-2.0 and which later tasks add; Apache-2.0 is
compatible with GPLv3 but not with GPLv2-only. The APK carries no vendor SDK, AAR or header. The
sources stay under their own licences; only the combined work is GPLv3-or-later. The app's about
text repeats this once it has one.
