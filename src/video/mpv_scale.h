#ifndef XRD_VIDEO_MPV_SCALE_H
#define XRD_VIDEO_MPV_SCALE_H

/* --mpv-scale: the size of the video texture when mpv, not the eye pass,
 * enlarges a flat picture, and the debounce that keeps zoom presses from
 * reallocating it on every step. Pure; no GL or mpv. */

#include <stdbool.h>
#include <stdint.h>

#include "render/projection.h"

/* The enlarged texture holds at most this many texels, both eyes together:
 * a 3840x2160 frame, which mpv already renders for a 4K film. */
#define XRD_MPV_SCALE_MAX_TEXELS (3840L * 2160L)
/* A new texture size takes effect once it has been wanted this long. */
#define XRD_MPV_SCALE_SETTLE_NS 250000000ull

/* The texture size at which each eye's sub-image maps about 1:1 to display
 * pixels at the centre of the view, for the flat screen of g (half_w, half_h
 * on the plane z = -1, tan_h, tan_v of the view). The frame's layout is kept:
 * side by side gives two such eyes side by side, top-bottom two stacked, mono
 * one; packing is already in g (the picture's aspect). tex_w x tex_h is the
 * coded texture (its eye is half of it on the split axis, rounded down, as the
 * eye pass reads it), out_eye_w x out_eye_h one eye's viewport.
 *
 * Returns true, with the size in *fbo_w x *fbo_h, when the picture is
 * enlarged on both axes (xrd_minify_ratio < 1) and stays enlarged on both
 * after the cap: the whole texture is scaled down, keeping its shape, to at
 * most XRD_MPV_SCALE_MAX_TEXELS texels and max_tex (GL_MAX_TEXTURE_SIZE; <= 0
 * for none) on each side. Otherwise false, with tex_w x tex_h: the coded size,
 * today's path (mipmaps when shrunk, the eye pass's filter otherwise). Also
 * false for a geometry with no flat screen (VR180) or any size that is not
 * > 0 or not finite. */
bool xrd_mpv_scale_size(const xrd_view_geom *g, xrd_layout layout, int tex_w, int tex_h,
                        int out_eye_w, int out_eye_h, int max_tex, int *fbo_w, int *fbo_h);

/* Holds the texture size in effect while a new one settles. A zero struct is
 * ready: nothing applied yet. */
typedef struct {
    int w, h;           /* the size in effect; 0 x 0 = the coded size */
    int want_w, want_h; /* the newest size asked for that differs from w x h */
    uint64_t since_ns;  /* when want_w x want_h was first asked for */
    bool pending;       /* want_w x want_h is waiting to settle */
    bool started;       /* a size has been applied */
} xrd_size_debounce;

/* Feeds the size wanted now (0 x 0 for the coded size) at now_ns. The first
 * call applies it at once. After that a different size takes effect once it
 * has been asked for on every call for settle_ns; asking for another size in
 * between starts the wait again, and asking for the size in effect cancels it.
 * Returns true when d->w x d->h changed. */
bool xrd_size_debounce_step(xrd_size_debounce *d, int w, int h, uint64_t now_ns,
                            uint64_t settle_ns);

#endif
