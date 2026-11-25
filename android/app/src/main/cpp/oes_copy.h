#ifndef XRD_OES_COPY_H
#define XRD_OES_COPY_H

/* The copy pass (spec 4.2 B, 5.3 GL): one new video frame, the decoder's SurfaceTexture (an
 * external OES texture), drawn into an ordinary GL_RGB10_A2 texture of the video's size that the
 * shared eye shader samples. Android only; oes_copy.frag is its shader and the only OES code. */

#include <stdbool.h>
#include <stddef.h>

typedef struct xrd_oes_copy xrd_oes_copy;

/* Needs a current GLES 3.0 context (the render thread's). Compiles vr180.vert's fullscreen
 * triangle with oes_copy.frag. NULL, with err filled, on failure. */
xrd_oes_copy *xrd_oes_copy_new(char *err, size_t errlen);

/* Frees the program, the framebuffer and the target texture; NULL is accepted. Needs the
 * context current. */
void xrd_oes_copy_free(xrd_oes_copy *c);

/* Draws oes_tex (GL_TEXTURE_EXTERNAL_OES) through st (SurfaceTexture.getTransformMatrix(),
 * column-major 4x4) into the target texture, which is (re)allocated as w x h GL_RGB10_A2
 * (GL_LINEAR, clamp to edge, one level: the contract of xrd_renderer_draw) on the first call and
 * whenever the size changes. The target is GL-oriented like every video texture (t = 1 the image
 * top), as SurfaceTexture's transform gives it. false, with err filled, when w or h is not
 * positive or the target cannot be rendered to; the target texture is then 0.
 *
 * Changes the bound program, VAO, the active texture unit (to 0) and its external-OES binding,
 * the viewport, and disables GL_SCISSOR_TEST, GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST,
 * GL_STENCIL_TEST; leaves the draw framebuffer bound to 0 (the window). */
bool xrd_oes_copy_draw(xrd_oes_copy *c, unsigned oes_tex, const float st[16], int w, int h,
                       char *err, size_t errlen);

/* The target texture (0 before a successful xrd_oes_copy_draw) and its size. */
unsigned xrd_oes_copy_texture(const xrd_oes_copy *c);
void xrd_oes_copy_size(const xrd_oes_copy *c, int *w, int *h);

#endif
