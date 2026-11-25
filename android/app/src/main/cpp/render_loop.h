#ifndef XRD_RENDER_LOOP_H
#define XRD_RENDER_LOOP_H

/* What the render thread draws on the glasses (spec 5.3 GL, Frame pacing): one EGL ES 3.0 context
 * and window surface, the shared renderer, the OES texture the decoder's SurfaceTexture fills,
 * the copy pass, and a static test pattern. Every function runs on the one thread that called
 * xrd_render_loop_new (RenderThread.kt), where the context stays current. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ANativeWindow;
typedef struct xrd_render_loop xrd_render_loop;

/* Makes the context and the window surface on win (the Presentation's Surface; the caller keeps
 * its reference to win until xrd_render_loop_free has returned), current on this thread with
 * swap interval 1, then the renderer, the OES texture, the copy pass and the tracker (null pose
 * source: identity view in this task). out_w x out_h is the surface size, 3840 x 1080 on the
 * glasses; fov_diag the glasses' diagonal FOV (xrd_device_builtin). NULL, with err filled and
 * nothing left current, on failure. */
xrd_render_loop *xrd_render_loop_new(struct ANativeWindow *win, int out_w, int out_h,
                                     float fov_diag, char *err, size_t errlen);

/* Releases everything xrd_render_loop_new made and leaves no context current. NULL is accepted. */
void xrd_render_loop_free(xrd_render_loop *rl);

/* The GL_TEXTURE_EXTERNAL_OES texture name for new SurfaceTexture(name). */
unsigned xrd_render_loop_oes_texture(const xrd_render_loop *rl);

/* The size of the video frames the SurfaceTexture receives (the decoder's output size); the copy
 * target is (re)allocated to it on the next new frame. 0 x 0 until set: new frames are then not
 * copied. */
void xrd_render_loop_set_video_size(xrd_render_loop *rl, int w, int h);

/* Draw the static 2:1 SBS test pattern (each eye half in four solid colours, test_render.c's
 * quadrant texture) instead of the video, from the next draw on. */
void xrd_render_loop_show_pattern(xrd_render_loop *rl);

/* One display vsync: on new_frame (the caller has just called updateTexImage), the copy pass
 * draws the OES texture through st (column-major 4x4) into the copy target; then the eye pass
 * draws the pattern or the copy target (black before the first copy) at zoom 1, half-equirect,
 * SBS left-right, and the surface is swapped. frame_time_ns is the Choreographer frame time
 * (unused until the timing log, Task 6). false, with err filled, when the copy pass or the swap
 * failed (a lost surface): the caller stops drawing. */
bool xrd_render_loop_draw(xrd_render_loop *rl, bool new_frame, const float st[16],
                          int64_t frame_time_ns, char *err, size_t errlen);

#endif
