xrdome @VERSION@ for macOS (Apple Silicon), a test build
=========================================================

This is a test build for checking xrdome on XR glasses with a Mac. It was
built by CI on @BUILT_ON@ (arm64); an older macOS may refuse to run it.
It needs no Homebrew: the libraries are in lib/. It is not notarised.

1. Unzip, then in Terminal:

     cd xrdome
     xattr -dr com.apple.quarantine .

   (macOS marks downloaded programs; without this it says "cannot be opened".)

2. Check that it starts:

     ./xrdome --version

3. Play a video on the glasses. Connect them first and switch them to
   side-by-side (SBS) mode with their own button (the automatic switch needs
   Linux). Set the glasses' display to 3840x1080 at 100% scale if macOS offers
   that mode.

     ./xrdome --pose null --require-hwdec --timing-log mac.csv --frames 3600 video.mp4

   --pose null drags the view with the mouse; without --frames it plays to
   the end. For a window on the main display instead, add --windowed.

4. Report what you saw at @ISSUE_URL@:
   your Mac model and macOS version, the glasses, what appeared on which
   display, whether it ran smoothly, the terminal output, and mac.csv.

ffprobe (next to xrdome) is found automatically; xrdome uses it to tell VR180
from flat video. Source and licences: COPYING (xrdome, GPL-2.0-or-later),
LICENSES/ (each library), SOURCES.md (where to get every source).
