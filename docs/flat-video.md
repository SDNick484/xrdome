# Flat video

An ordinary film (2D, or 3D packed side by side or top and bottom) is shown as a flat
rectangular screen in front of you, fixed in the room: turn your head and the screen stays where
it is, so a screen larger than the glasses' view can be explored by looking around, "almost pan
and scan". Straight lines in the video stay straight at every head angle. The screen sits at the
glasses' own optical distance; there is no distance control. 3D films use the depth in the file
as it is.

At zoom 1 the whole picture just fits the display, as in plain mpv (a 2.39:1 film is
letterboxed, a 4:3 picture pillarboxed). The default flat zoom is 1.5 (`flat_zoom` in
`config.ini`, separate from the VR180 `zoom`), so the screen is larger than the view. `[` and `]`
change it, x1.1 per step from 0.25 to 4. Each projection keeps its own zoom during a run, so `P`
switches to the other projection's zoom rather than carrying the number over.

Two zoom modes (`Z`, or `--zoom-mode`, remembered per file):

- `screen` (default): zoom sets the size of the screen. The view keeps the display's true field
  of view, so the picture moves exactly as much as your head turns.
- `magnify`: the screen stays at the size of zoom 1 and zoom narrows the view instead, as zoom
  does for VR180. The picture moves zoom times faster than your head.

At the same zoom both modes show the same point of the picture at the centre of the view, at the
same scale, so switching does not jump. On a flat screen they show the same picture across the
whole view straight ahead; on a curved one they differ away from the centre (see below).

Screen size of a 16:9 picture on the Viture Pro XR (display 37.9 x 21.9 degrees), in screen mode,
as `I` prints it:

| Zoom | Screen (degrees) |
|---|---|
| 1.0 | 37.9 x 21.9 |
| 1.5 (default) | 54.5 x 32.3 |
| 2.0 | 69.0 x 42.2 |
| 3.0 | 91.7 x 60.2 |

Screen lock (`L`, `--screen fixed|locked`, `[general] screen`): locked, the screen follows your
head like the glasses' own mode (head tracking and mouse look are ignored; convergence still
applies). Turning the lock off recentres fully, so the screen reappears straight ahead; a tilted
head at that moment gives a tilted screen, as with `Shift+R`. The lock is not saved per file,
since it depends on where you sit, not on the film. It does nothing for VR180.

Curved screen (`C`, `--screen-curve off|R`, `[general] screen_curve`, default `off`): the screen
is bent into part of an upright cylinder, so the edges of a large screen are as far from you as
its centre and face you instead of being seen at a slant. R is the cylinder's radius in screen
distances, from 1 to 10. The screen's centre stays where the flat screen's is and the cylinder
touches the flat screen there, so R = 1 puts the cylinder's axis through your head and every
column of the screen at the same distance; a larger R bends it less and approaches the flat
screen. The screen keeps its width measured along the curve, and its height, so a curved screen
at a given zoom covers the same picture as a flat one but its ends come round towards you: at
zoom 1 with R = 1, a 16:9 screen's left and right edges sit just outside the view (39.3 degrees
apart instead of 37.9). Columns stay straight and upright; the top and bottom edges bow outwards
towards the sides, as the top and bottom of a cinema's curved screen do from its centre. `C`
switches between flat and curved; if `--screen-curve` was `off`, `C` curves with R = 1. Like the
lock, the curve is not saved per file. Magnify mode curves too: its zoom-1 screen is bent with
the same R (the arc keeps the zoom-1 screen's width, the height is unchanged) and zoom narrows the
view onto it, so `Z` never changes the curve. Straight ahead the two modes show the same point of
the picture at the view's centre, at the same scale; away from it they differ. In screen mode the
arc is zoom times longer and bends more across the view; in magnify mode the view lies on the
middle of the zoom-1 arc, which bends little there, so across the view the picture is nearer the
flat screen's: at zoom 2.5 with R = 1 the view's left and right edges show about 3% less of the
picture in screen mode than in magnify mode. Above zoom 1 the magnified view of a 16:9 picture
straight ahead lies wholly on the screen, so the arc's ends and the bowed top and bottom come into
view only when you turn towards them. A head-locked screen curves the same way. Nothing beside or
behind you is drawn, so an arc that would reach past 90 degrees to either side stops there (at
zoom 4 with R = 1 a 16:9 screen reaches 79 degrees).

Subtitles: for flat 2D, mpv's own subtitle rendering is left on, so a subtitle track (or an
`.srt` next to the file) appears on the screen, at the bottom of the picture. In 3D layouts and in
VR180 subtitles are turned off, because mpv would draw them once across the whole frame and split
them between the eyes; `M` and `P` switch them with the layout. `--mpv-opt sub-visibility=no`
turns them off for flat 2D too.
