/* The glasses' render loop: EGL ES 3.0 on the render thread, the copy pass, the shared eye pass,
 * the test pattern (spec 5.3 GL, Frame pacing). See render_loop.h. */
#include "render_loop.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oes_copy.h"
#include "pose/pose.h"
#include "render/projection.h"
#include "render/renderer_gl.h"
#include "track/tracker.h"
#include "util/timing.h"

/* GL_OES_EGL_image_external; defined here rather than mixing in GLES2/gl2ext.h. */
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

/* The test pattern: 2:1, each eye half in four solid colours. An even width, so the SBS split
 * falls between texels and no filter mixes the eyes. */
#define PATTERN_W 512
#define PATTERN_H 256

struct xrd_render_loop {
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface surf;
    int out_w, out_h;
    float fov_diag;
    xrd_renderer *r;
    xrd_oes_copy *copy;
    GLuint oes_tex;
    GLuint pattern_tex; /* 0 until xrd_render_loop_show_pattern */
    bool show_pattern;
    int video_w, video_h;
    xrd_pose_source *pose;
    xrd_tracker *trk;
};

static void tracker_log(const char *msg, void *user)
{
    (void)user;
    __android_log_print(ANDROID_LOG_INFO, "xrdome", "tracker: %s", msg);
}

/* Colour of quadrant (eye, right half?, top half?): test_render.c's quadrant texture, so the
 * picture on the glasses can be compared with the Linux renderer's (hardware check H2). */
static const uint8_t *quad_colour(int eye, int right, int top)
{
    static const uint8_t c[2][2][2][3] = {
        /* eye 0 */ {{{0, 0, 255}, {255, 0, 0}},       /* left:  bottom, top */
                     {{128, 0, 255}, {255, 128, 0}}},  /* right: bottom, top */
        /* eye 1 */ {{{0, 255, 255}, {255, 255, 0}},
                     {{255, 255, 255}, {255, 0, 255}}},
    };
    return c[eye][right][top];
}

static GLuint make_pattern(void)
{
    uint8_t *px = malloc((size_t)PATTERN_W * PATTERN_H * 4);
    if (!px)
        return 0;
    /* Row 0 is t = 0, the image bottom (GL orientation, as every video texture). */
    for (int y = 0; y < PATTERN_H; y++)
        for (int x = 0; x < PATTERN_W; x++) {
            uint8_t *p = &px[((size_t)y * PATTERN_W + x) * 4];
            const uint8_t *c = quad_colour(x / (PATTERN_W / 2),
                                           (x % (PATTERN_W / 2)) >= PATTERN_W / 4,
                                           y >= PATTERN_H / 2);
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
            p[3] = 255;
        }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PATTERN_W, PATTERN_H, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(px);
    return t;
}

static bool egl_fail(xrd_render_loop *rl, const char *what, char *err, size_t errlen)
{
    snprintf(err, errlen, "%s (EGL error 0x%x)", what, (unsigned)eglGetError());
    xrd_render_loop_free(rl);
    return false;
}

static bool egl_setup(xrd_render_loop *rl, struct ANativeWindow *win, char *err, size_t errlen)
{
    rl->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (rl->dpy == EGL_NO_DISPLAY || !eglInitialize(rl->dpy, NULL, NULL)) {
        rl->dpy = EGL_NO_DISPLAY;
        return egl_fail(rl, "eglInitialize failed", err, errlen);
    }
    static const EGLint cfg_attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(rl->dpy, cfg_attrs, &cfg, 1, &ncfg) || ncfg < 1)
        return egl_fail(rl, "no EGL config with ES 3.0 and a window surface", err, errlen);

    static const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    rl->ctx = eglCreateContext(rl->dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (rl->ctx == EGL_NO_CONTEXT)
        return egl_fail(rl, "could not create an ES 3.0 context", err, errlen);
    rl->surf = eglCreateWindowSurface(rl->dpy, cfg, (EGLNativeWindowType)win, NULL);
    if (rl->surf == EGL_NO_SURFACE)
        return egl_fail(rl, "could not create the window surface on the glasses", err, errlen);
    if (!eglMakeCurrent(rl->dpy, rl->surf, rl->surf, rl->ctx))
        return egl_fail(rl, "could not make the ES 3.0 context current", err, errlen);
    /* One swap per vsync of the glasses: the swap blocks when the queue is full, so a
     * Choreographer tick faster than the glasses (a 120 Hz phone screen) cannot outrun them. */
    eglSwapInterval(rl->dpy, 1);
    return true;
}

xrd_render_loop *xrd_render_loop_new(struct ANativeWindow *win, int out_w, int out_h,
                                     float fov_diag, char *err, size_t errlen)
{
    if (!win || out_w <= 0 || out_h <= 0 || !(fov_diag > 0.0f)) {
        snprintf(err, errlen, "render loop: bad arguments (%dx%d, fov %.1f)", out_w, out_h,
                 (double)fov_diag);
        return NULL;
    }
    xrd_render_loop *rl = calloc(1, sizeof *rl);
    if (!rl) {
        snprintf(err, errlen, "render loop: out of memory");
        return NULL;
    }
    rl->dpy = EGL_NO_DISPLAY;
    rl->ctx = EGL_NO_CONTEXT;
    rl->surf = EGL_NO_SURFACE;
    rl->out_w = out_w;
    rl->out_h = out_h;
    rl->fov_diag = fov_diag;
    if (!egl_setup(rl, win, err, errlen))
        return NULL; /* egl_fail freed rl */

    rl->r = xrd_renderer_new(err, errlen);
    if (!rl->r)
        goto fail;
    rl->copy = xrd_oes_copy_new(err, errlen);
    if (!rl->copy)
        goto fail;

    glGenTextures(1, &rl->oes_tex);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, rl->oes_tex);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    /* Identity view in this task: the null pose source never has a sample. The tracker is here
     * so the Viture backend (Task 8) only swaps the source. */
    rl->pose = xrd_pose_source_create("null", err, errlen);
    if (!rl->pose)
        goto fail;
    rl->trk = xrd_tracker_new(rl->pose);
    if (!rl->trk) {
        snprintf(err, errlen, "render loop: out of memory");
        goto fail;
    }
    xrd_tracker_set_log(rl->trk, tracker_log, NULL);
    return rl;

fail:
    xrd_render_loop_free(rl);
    return NULL;
}

void xrd_render_loop_free(xrd_render_loop *rl)
{
    if (!rl)
        return;
    xrd_tracker_free(rl->trk);
    xrd_pose_source_destroy(rl->pose);
    if (rl->dpy != EGL_NO_DISPLAY && rl->ctx != EGL_NO_CONTEXT &&
        eglGetCurrentContext() == rl->ctx) {
        /* GL objects need the context current; it is from xrd_render_loop_new on. */
        xrd_oes_copy_free(rl->copy);
        xrd_renderer_free(rl->r);
        if (rl->oes_tex)
            glDeleteTextures(1, &rl->oes_tex);
        if (rl->pattern_tex)
            glDeleteTextures(1, &rl->pattern_tex);
    }
    if (rl->dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(rl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (rl->surf != EGL_NO_SURFACE)
            eglDestroySurface(rl->dpy, rl->surf);
        if (rl->ctx != EGL_NO_CONTEXT)
            eglDestroyContext(rl->dpy, rl->ctx);
        /* No eglTerminate: the display is the process's one EGL_DEFAULT_DISPLAY, and other
         * users (a second loop, the device report) would lose it with us. */
        eglReleaseThread();
    }
    free(rl);
}

unsigned xrd_render_loop_oes_texture(const xrd_render_loop *rl)
{
    return rl->oes_tex;
}

void xrd_render_loop_set_video_size(xrd_render_loop *rl, int w, int h)
{
    rl->video_w = w > 0 ? w : 0;
    rl->video_h = h > 0 ? h : 0;
}

void xrd_render_loop_show_pattern(xrd_render_loop *rl)
{
    if (!rl->pattern_tex)
        rl->pattern_tex = make_pattern();
    rl->show_pattern = rl->pattern_tex != 0;
}

bool xrd_render_loop_draw(xrd_render_loop *rl, bool new_frame, const float st[16],
                          int64_t frame_time_ns, char *err, size_t errlen)
{
    (void)frame_time_ns; /* the timing log (Task 6) */

    if (new_frame && rl->video_w > 0 && rl->video_h > 0 &&
        !xrd_oes_copy_draw(rl->copy, rl->oes_tex, st, rl->video_w, rl->video_h, err, errlen))
        return false;

    GLuint tex = rl->show_pattern ? rl->pattern_tex : xrd_oes_copy_texture(rl->copy);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, rl->out_w, rl->out_h);
    if (tex) {
        /* The pose is sampled as late as possible: right before the eye pass (as on Linux). */
        xrd_quat view = xrd_tracker_view(rl->trk, xrd_now_ns());
        float rot[9];
        xrd_quat_to_mat3(view, rot);
        xrd_draw_params dp = {
            .proj = XRD_PROJ_HEQUIRECT,
            .layout = XRD_LAYOUT_SBS_LR,
            .trilinear = false, /* no mipmaps in M3 (spec 5.3) */
        };
        float eye_aspect = (float)(rl->out_w / 2) / (float)rl->out_h;
        xrd_view_geometry(XRD_PROJ_HEQUIRECT, XRD_ZOOM_SCREEN, rl->fov_diag, eye_aspect, 1.0f,
                          1.0, &dp.geom);
        xrd_renderer_draw(rl->r, tex, rot, &dp, rl->out_w, rl->out_h);
    } else {
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (!eglSwapBuffers(rl->dpy, rl->surf)) {
        snprintf(err, errlen, "eglSwapBuffers failed (EGL error 0x%x): the glasses' surface is "
                              "gone", (unsigned)eglGetError());
        return false;
    }
    return true;
}
