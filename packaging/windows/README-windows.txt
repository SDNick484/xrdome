xrdome @VERSION@ for Windows 11 (x64)
=====================================

xrdome plays VR180 and flat video on XR glasses. This build was made by
CI on @BUILT_ON@. It needs no installation and no MSYS2: unzip the folder
anywhere (a path with spaces or non-English letters is fine) and run
xrdome.exe from there. ffprobe.exe, next to it, is found automatically; xrdome
uses it to tell VR180 from flat video.

It is not signed. If Windows says "Windows protected your PC", choose "More
info", then "Run anyway". Or, before unzipping, right-click the zip, Properties,
tick "Unblock".

1. Set up the glasses
   - Connect them and switch them to side-by-side (SBS) mode with their own
     button: the automatic switch needs Linux. The glasses' display must be
     3840x1080.
   - In Settings > System > Display, set the glasses' display to 100% scale.
     xrdome places its window in pixels; a scaled display shows it the wrong
     size.

2. Check that it starts, in PowerShell or cmd, from this folder:

     .\xrdome.exe --version

3. Play a video on the glasses:

     .\xrdome.exe --pose null --timing-log win.csv --frames 3600 video.mp4

   --pose null drags the view with the mouse; without --frames it plays to the
   end. For a window on the main display instead, add --windowed. Do not pass
   --require-hwdec: Windows decodes on the GPU (d3d11va) and copies the frame,
   which that option refuses. Head tracking comes over UDP in OpenTrack's
   format (for example from opentrack itself).

   Not available on Windows: the depth helper (3D from flat photos and films).

4. Where things are kept
   - Settings:        %APPDATA%\xrdome\config.ini   (create it if you want one)
   - Per-file records: %LOCALAPPDATA%\xrdome\files\
   Delete either to start over.

5. Report what you saw at @ISSUE_URL@:
   your Windows version, the glasses, what appeared on which display, whether
   it ran smoothly, the terminal output, and win.csv.

Licences and source: xrdome is GPL-2.0-or-later (COPYING). The DLLs and
ffprobe.exe are unmodified MSYS2 packages, some of them GPL version 3, so this
folder as a whole is distributed under GPL version 3 or later. LICENSES/ has each
package's licence files, and SOURCES.md names the exact version and the source
archive of every package, which is the offer of source for them.
