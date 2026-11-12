#ifndef XRD_RENDERER_GL_H
#define XRD_RENDERER_GL_H

#include <stdbool.h>
#include <stddef.h>

#include "render/projection.h"

typedef struct xrd_renderer xrd_renderer;

typedef enum { XRD_FILTER_BILINEAR, XRD_FILTER_BICUBIC } xrd_filter;

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

/* Draws both eyes into the currently bound framebuffer: left eye in the left half.
 * rot is column-major (xrd_quat_to_mat3).
 *
 * Contract:
 *  - video_tex must not be 0. It is GL-oriented (t = 1 is the image top). Its
 *    owner must set GL_LINEAR min/mag filters and GL_CLAMP_TO_EDGE on both axes.
 *  - tan_h/tan_v must be computed with the per-eye aspect (out_w / 2) / out_h.
 *  - draw disables GL_SCISSOR_TEST, GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST,
 *    GL_STENCIL_TEST and GL_FRAMEBUFFER_SRGB and enables all colour writes.
 *  - draw changes the bound program, VAO, active texture unit, the unit-0 2D
 *    texture binding, the viewport and the flags above, and does not restore them. */
void xrd_renderer_draw(xrd_renderer *r, unsigned video_tex, const float rot[9],
                       float tan_h, float tan_v, xrd_layout layout, bool swap_eyes,
                       int out_w, int out_h);

#endif
