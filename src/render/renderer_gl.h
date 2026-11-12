#ifndef XRD_RENDERER_GL_H
#define XRD_RENDERER_GL_H

#include <stdbool.h>
#include <stddef.h>

#include "render/projection.h"

typedef struct xrd_renderer xrd_renderer;

/* Needs a current GL 3.3 core context. Returns NULL and fills err on failure. */
xrd_renderer *xrd_renderer_new(char *err, size_t errlen);
void xrd_renderer_free(xrd_renderer *r);

/* Draws both eyes into the currently bound framebuffer: left eye in the left half.
 * rot is column-major (xrd_quat_to_mat3). */
void xrd_renderer_draw(xrd_renderer *r, unsigned video_tex, const float rot[9],
                       float tan_h, float tan_v, xrd_layout layout, bool swap_eyes,
                       int out_w, int out_h);

#endif
