#ifndef XRD_RENDERER_GL_H
#define XRD_RENDERER_GL_H

#include <stdbool.h>
#include <stddef.h>

#include "render/projection.h"

typedef struct xrd_renderer xrd_renderer;

typedef enum { XRD_FILTER_BILINEAR, XRD_FILTER_BICUBIC } xrd_filter;

/* Highest mipmap level the eye pass samples with xrd_draw_params.trilinear;
 * the video texture's chain holds levels 0 to this. */
#define XRD_MIP_MAX_LEVEL 3

/* Needs a current GL 3.3 core context. Returns NULL and fills err on failure. */
xrd_renderer *xrd_renderer_new(char *err, size_t errlen);
void xrd_renderer_free(xrd_renderer *r);

/* The video covers hfov_deg x vfov_deg degrees, centred straight ahead (see
 * xrd_dir_to_uv_fov); directions outside draw black. Default 180 x 180. Both
 * must be in (0, 180]. Takes effect on the next xrd_renderer_draw. */
void xrd_renderer_set_content_fov(xrd_renderer *r, float hfov_deg, float vfov_deg);

/* Stereo convergence in degrees (default 0): each eye's view is yawed by half
 * of it, in opposite directions, after the view rotation. Positive toes the
 * eyes in (the left eye looks right, the right eye looks left), which moves
 * the left viewport's image left and the right viewport's image right. It
 * belongs to the viewports, so it applies the same with swapped eyes and in
 * mono. Takes effect on the next xrd_renderer_draw. */
void xrd_renderer_set_convergence(xrd_renderer *r, float deg);

/* Eye-pass sampling of the video texture (default XRD_FILTER_BILINEAR).
 * XRD_FILTER_BICUBIC is Catmull-Rom, clamped to the eye's sub-image like the
 * bilinear path. Takes effect on the next xrd_renderer_draw. */
void xrd_renderer_set_filter(xrd_renderer *r, xrd_filter f);

/* Ordered dithering of the 8-bit output (default off): adds a per-pixel offset
 * in [-0.5, 0.5) of one 8-bit level to r, g and b (interleaved gradient noise
 * from the window position), so a video texture with more than 8 bits keeps
 * its precision on average instead of banding. The black drawn behind the
 * viewer and outside the content is not dithered. Takes effect on the next
 * xrd_renderer_draw. */
void xrd_renderer_set_dither(xrd_renderer *r, bool on);

typedef struct {
    xrd_proj proj;
    xrd_layout layout;
    bool swap_eyes;
    xrd_view_geom geom;
    bool trilinear;   /* sample through the mipmap chain; the caller guarantees it is valid */
} xrd_draw_params;

/* Draws both eyes into the currently bound framebuffer: left eye in the left half.
 * rot is column-major (xrd_quat_to_mat3).
 *
 * dp->proj selects the mapping: XRD_PROJ_HEQUIRECT is the half-equirect lookup
 * of xrd_dir_to_uv_fov (with the content FOV), XRD_PROJ_FLAT the screen of
 * xrd_flat_dir_to_uv with dp->geom.half_w / half_h. Directions that miss the
 * picture draw black. dp->layout and dp->swap_eyes pick each eye's sub-image
 * (xrd_eye_uv_transform); when a split axis has an odd number of texels, the
 * middle texel column (sbs) or row (tb) belongs to neither eye, so no filter
 * ever reads one eye's pixels for the other.
 *
 * Contract:
 *  - video_tex must not be 0. It is GL-oriented (t = 1 is the image top). Its
 *    owner must set GL_LINEAR mag filter and GL_CLAMP_TO_EDGE on both axes.
 *  - dp must not be NULL. dp->geom.tan_h/tan_v must be computed with the
 *    per-eye aspect (out_w / 2) / out_h (xrd_view_geometry does).
 *  - dp->trilinear: the eye pass samples trilinearly (GL_LINEAR_MIPMAP_LINEAR
 *    at a level from the screen-space derivatives, clamped to 0 to
 *    XRD_MIP_MAX_LEVEL) and Catmull-Rom is not used. The texture's min filter
 *    must then be GL_LINEAR_MIPMAP_LINEAR and levels 1 to XRD_MIP_MAX_LEVEL
 *    must have been generated (glGenerateMipmap) from the current level 0. On
 *    a split axis the lookup is clamped away from the eye's edges by a margin
 *    that grows with the highest level read, so no level mixes in texels of
 *    the other eye or the middle texel of an odd axis, for any generation
 *    filter that draws a new texel only from source texels within its
 *    footprint widened by one source texel each side (box, bilinear blit,
 *    4-tap tent). The margin is up to 22.5 level-0 texels at level 3, under
 *    3 display pixels at 8x shrink.
 *    Without trilinear only level 0 is read, whatever the min filter (the
 *    texture must still be complete for it).
 *  - draw disables GL_SCISSOR_TEST, GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST,
 *    GL_STENCIL_TEST and GL_FRAMEBUFFER_SRGB and enables all colour writes.
 *  - draw changes the bound program, VAO, active texture unit, the unit-0 2D
 *    texture binding, the viewport and the flags above, and does not restore them. */
void xrd_renderer_draw(xrd_renderer *r, unsigned video_tex, const float rot[9],
                       const xrd_draw_params *dp, int out_w, int out_h);

#endif
