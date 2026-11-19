#define _POSIX_C_SOURCE 200809L /* getpid */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include "video/framesink.h"

/* The frame sink on the GPU (a surfaceless Mesa EGL context, as test_render):
 * a video texture scaled to 322x182, read back without waiting, in the ring
 * top row first. */

#define TEX_W 640
#define TEX_H 360

static EGLDisplay g_dpy;
static EGLContext g_ctx;
static GLuint g_tex;

static int setup(void **state)
{
    (void)state;
    g_dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint maj, min;
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, &maj, &min) || !eglBindAPI(EGL_OPENGL_API))
        return -1;
    const EGLint cattr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
    if (g_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx))
        return -1;
    /* GL-oriented like mpv's texture: row 0 is the bottom. Top half red,
     * bottom half blue; the left half also green. */
    static uint8_t px[TEX_W * TEX_H * 4];
    for (int y = 0; y < TEX_H; y++)
        for (int x = 0; x < TEX_W; x++) {
            uint8_t *p = &px[(y * TEX_W + x) * 4];
            bool top = y >= TEX_H / 2;
            p[0] = top ? 255 : 0;
            p[1] = x < TEX_W / 2 ? 255 : 0;
            p[2] = top ? 0 : 255;
            p[3] = 255;
        }
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    /* as the player may leave it with mipmaps on: the sink reads level 0 */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    glDeleteTextures(1, &g_tex);
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(g_dpy, g_ctx);
    eglTerminate(g_dpy);
    return 0;
}

static xrd_framesink *new_sink(unsigned n)
{
    char name[32], err[512] = "";
    assert_int_equal(xrd_vring_shm_name(name, sizeof name, (long)getpid(), n), 0);
    xrd_framesink *fs = xrd_framesink_new(name, err, sizeof err);
    if (!fs)
        fail_msg("xrd_framesink_new: %s", err);
    return fs;
}

/* Steps without a new frame until the readbacks in flight are in the ring
 * (the GPU is given time with glFinish, which the player never does). */
static void drain(xrd_framesink *fs, unsigned long serial, uint64_t want_sent)
{
    xrd_framesink_stats st;
    for (int i = 0; i < 100; i++) {
        glFinish();
        xrd_framesink_step(fs, g_tex, serial, 1000 + (uint64_t)i);
        xrd_framesink_get_stats(fs, &st);
        if (st.sent >= want_sent)
            return;
    }
    fail_msg("only %llu of %llu frames reached the ring", (unsigned long long)st.sent,
             (unsigned long long)want_sent);
}

static const uint8_t *pixel(const uint8_t *frame, int x, int y)
{
    return frame + ((size_t)y * XRD_VRING_W + (size_t)x) * 4;
}

static void scaled_top_row_first_one_frame_late(void **state)
{
    (void)state;
    xrd_framesink *fs = new_sink(1);
    void *ring = xrd_framesink_ring(fs);
    xrd_framesink_step(fs, g_tex, 7, 1000);
    xrd_framesink_stats st;
    xrd_framesink_get_stats(fs, &st);
    assert_int_equal(st.started, 1);
    assert_int_equal(st.sent, 0); /* read back later, never in the same step */
    assert_int_equal(xrd_vring_frame_head(ring), 0);
    drain(fs, 7, 1);
    xrd_framesink_get_stats(fs, &st);
    assert_int_equal(st.started, 1); /* the same serial is not sent again */
    assert_int_equal(xrd_vring_frame_head(ring), 1);

    static uint8_t f[XRD_VRING_FRAME_BYTES];
    uint64_t seen = 0, t = 0;
    assert_true(xrd_vring_frame_read(ring, &seen, f, &t));
    assert_true(t >= 1000);
    const uint8_t *tl = pixel(f, 0, 0), *br = pixel(f, XRD_VRING_W - 1, XRD_VRING_H - 1);
    assert_true(tl[0] > 250 && tl[1] > 250 && tl[2] < 5); /* top left: red + green */
    assert_true(br[0] < 5 && br[1] < 5 && br[2] > 250);   /* bottom right: blue */
    const uint8_t *tr = pixel(f, XRD_VRING_W - 1, 10), *bl = pixel(f, 5, XRD_VRING_H - 10);
    assert_true(tr[0] > 250 && tr[1] < 5 && tr[2] < 5);
    assert_true(bl[0] < 5 && bl[1] > 250 && bl[2] > 250);
    /* the GL state the player relies on is back at 0 */
    GLint fb = -1, pack = -1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
    assert_int_equal(fb, 0);
    assert_int_equal(pack, 0);
    assert_int_equal(glGetError(), GL_NO_ERROR);
    xrd_framesink_free(fs);
}

static void each_new_frame_numbered_in_order(void **state)
{
    (void)state;
    xrd_framesink *fs = new_sink(2);
    void *ring = xrd_framesink_ring(fs);
    for (unsigned long s = 1; s <= 5; s++) {
        glFinish();
        xrd_framesink_step(fs, g_tex, s, s);
    }
    drain(fs, 5, 5);
    xrd_framesink_stats st;
    xrd_framesink_get_stats(fs, &st);
    assert_int_equal(st.started + st.skipped, 5);
    assert_int_equal(st.sent, st.started);
    assert_int_equal(xrd_vring_frame_head(ring), st.sent);
    xrd_framesink_free(fs);
}

static void no_texture_no_readback(void **state)
{
    (void)state;
    xrd_framesink *fs = new_sink(3);
    xrd_framesink_step(fs, 0, 1, 1);
    xrd_framesink_stats st;
    xrd_framesink_get_stats(fs, &st);
    assert_int_equal(st.started, 0);
    xrd_framesink_step(fs, g_tex, 1, 2); /* in flight at free: dropped */
    xrd_framesink_free(fs);
    xrd_framesink_free(NULL);
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

int main(void)
{
    const char *ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!ext || !strstr(ext, "EGL_MESA_platform_surfaceless") ||
        !strstr(ext, "EGL_EXT_platform_base")) {
        fprintf(stderr, "surfaceless EGL platform unavailable; skipping\n");
        return 77;
    }
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(scaled_top_row_first_one_frame_late),
        cmocka_unit_test(each_new_frame_numbered_in_order),
        cmocka_unit_test(no_texture_no_readback),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}
