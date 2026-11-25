# Licence

GPL-2.0-or-later, the same terms as mpv built with its GPL files (see `Copyright` in the
[mpv repository](https://github.com/mpv-player/mpv)); xrdome links libmpv. The full text is in
`LICENSE`. The sharpening filter in `src/render/shaders/sharpen.frag` is a port of AMD FidelityFX
CAS and stays under AMD's MIT licence, whose notice is in that file (and in the shader text
compiled into the program); MIT is compatible with the GPL. `helpers/xrdome-pose-viture/` is a separate
program under the MIT licence (its own `LICENSE`); it shares no source with xrdome and never contains
VITURE's SDK, which is proprietary and is loaded from the user's own copy.

## The Android app

The Android APK (`android/`, built by the `android` CI workflow) is GPL-3.0-or-later as a whole. It
contains the xrdome core, which is GPL-2.0-or-later ("or later" lets it combine with Apache-2.0
code under GPLv3), and AndroidX Media3, which is Apache-2.0 and which later tasks add; Apache-2.0 is
compatible with GPLv3 but not with GPLv2-only. The APK carries no vendor SDK, AAR or header. The
sources stay under their own licences; only the combined work is GPLv3-or-later. The app's about
text repeats this once it has one.
