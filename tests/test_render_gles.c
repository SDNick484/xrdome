/* The shared renderer on OpenGL ES 3.0: the ESSL 3.00 rewrite of the shaders
 * (tools/embed_shaders.py) draws what the desktop shaders draw. Same surfaceless
 * EGL setup as test_render.c, but an ES 3.0 context; skips (77) when this
 * machine's Mesa cannot make one. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include <epoxy/egl.h>

#include "math/quat.h"
#include "render/gl.h"
#include "render/projection.h"
#include "render/renderer_gl.h"

#define W 384
#define H 108
#define TEX_W 64
#define TEX_H 32
#define EYE_ASPECT ((float)(W / 2) / (float)H)

static EGLDisplay g_dpy;
static EGLContext g_ctx;
static GLuint g_fbo, g_color, g_tex, g_quad_tex, g_flat_tex;
static xrd_renderer *g_r;
static float g_th, g_tv;
static uint8_t g_img[W * H * 4];

/* Colour of quadrant (eye, right half?, top half?), as in test_render.c. */
static const uint8_t *quad_colour(int eye, int right, int top)
{
    static const uint8_t c[2][2][2][3] = {
        {{{0, 0, 255}, {255, 0, 0}}, {{128, 0, 255}, {255, 128, 0}}},
        {{{0, 255, 255}, {255, 255, 0}}, {{255, 255, 255}, {255, 0, 255}}},
    };
    return c[eye][right][top];
}

static GLuint make_tex(int w, int h, const uint8_t *px)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static int setup(void **state)
{
    (void)state;
    g_dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint maj, min;
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, &maj, &min)) {
        fprintf(stderr, "eglInitialize failed: 0x%x\n", eglGetError());
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "eglBindAPI(ES) failed: 0x%x\n", eglGetError());
        return -1;
    }
    const EGLint cattr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
    if (g_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx)) {
        fprintf(stderr, "no ES 3.0 context: 0x%x\n", eglGetError());
        return -1;
    }

    glGenTextures(1, &g_color);
    glBindTexture(GL_TEXTURE_2D, g_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_color, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return -1;

    static uint8_t px[TEX_W * TEX_H * 4];
    for (int y = 0; y < TEX_H; y++)
        for (int x = 0; x < TEX_W; x++) {
            uint8_t *p = &px[(y * TEX_W + x) * 4];
            bool left = x < TEX_W / 2;
            p[0] = left ? 255 : 0;
            p[1] = 0;
            p[2] = left ? 0 : 255;
            p[3] = 255;
        }
    g_tex = make_tex(TEX_W, TEX_H, px);
    for (int y = 0; y < TEX_H; y++)
        for (int x = 0; x < TEX_W; x++) {
            uint8_t *p = &px[(y * TEX_W + x) * 4];
            const uint8_t *c = quad_colour(x / (TEX_W / 2), (x % (TEX_W / 2)) >= TEX_W / 4,
                                           y >= TEX_H / 2);
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
            p[3] = 255;
        }
    g_quad_tex = make_tex(TEX_W, TEX_H, px);
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        px[i * 4 + 0] = 0;
        px[i * 4 + 1] = 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }
    g_flat_tex = make_tex(TEX_W, TEX_H, px);

    char err[512] = "";
    g_r = xrd_renderer_new(err, sizeof err);
    if (!g_r) {
        fprintf(stderr, "renderer_new failed on ES: %s\n", err);
        return -1;
    }
    xrd_fov_tan_half(43.f, 16.f / 9.f, 1.f, &g_th, &g_tv);
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    xrd_renderer_free(g_r);
    return 0;
}

static void draw_dp(GLuint tex, xrd_quat q, const xrd_draw_params *dp)
{
    float rot[9];
    xrd_quat_to_mat3(q, rot);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, W, H);
    glClearColor(0.f, 1.f, 0.f, 1.f); /* green: any uncovered pixel fails */
    glClear(GL_COLOR_BUFFER_BIT);
    xrd_renderer_draw(g_r, tex, rot, dp, W, H);
    assert_int_equal(glGetError(), GL_NO_ERROR);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g_img);
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

static void draw_hequirect(GLuint tex, xrd_quat q)
{
    const xrd_draw_params dp = {.proj = XRD_PROJ_HEQUIRECT, .layout = XRD_LAYOUT_SBS_LR,
                                .geom = {.tan_h = g_th, .tan_v = g_tv}};
    xrd_renderer_set_content_fov(g_r, 180.f, 180.f);
    draw_dp(tex, q, &dp);
}

/* +-2 per channel: the desktop shaders' result for the same input. */
static void expect_px(int x, int y, int r, int g, int b)
{
    const uint8_t *p = &g_img[(y * W + x) * 4];
    int want[3] = {r, g, b};
    for (int c = 0; c < 3; c++)
        if (p[c] < want[c] - 2 || p[c] > want[c] + 2)
            fail_msg("pixel (%d,%d) = (%d,%d,%d), want (%d,%d,%d)", x, y, p[0], p[1], p[2], r, g,
                     b);
}

static void is_es(void **state)
{
    (void)state;
    assert_true(xrd_gl_is_es());
}

static void hequirect_centre_colour(void **state)
{
    (void)state;
    draw_hequirect(g_tex, XRD_QUAT_IDENTITY);
    expect_px(96, 54, 255, 0, 0);
    expect_px(288, 54, 0, 0, 255);
}

/* Each eye shows its own half of the picture: at zoom 1 the quadrant colours
 * of every eye, in the left and right halves, top and bottom, yawed to either side. */
static void sbs_eyes_not_mixed(void **state)
{
    (void)state;
    draw_hequirect(g_quad_tex, xrd_quat_from_ypr_deg(30, 0, 0));
    for (int eye = 0; eye < 2; eye++) {
        const uint8_t *top = quad_colour(eye, 0, 1), *bot = quad_colour(eye, 0, 0);
        expect_px(eye * (W / 2) + W / 4, H - 1, top[0], top[1], top[2]);
        expect_px(eye * (W / 2) + W / 4, 0, bot[0], bot[1], bot[2]);
    }
    draw_hequirect(g_quad_tex, xrd_quat_from_ypr_deg(-30, 0, 0));
    for (int eye = 0; eye < 2; eye++) {
        const uint8_t *top = quad_colour(eye, 1, 1), *bot = quad_colour(eye, 1, 0);
        expect_px(eye * (W / 2) + W / 4, H - 1, top[0], top[1], top[2]);
        expect_px(eye * (W / 2) + W / 4, 0, bot[0], bot[1], bot[2]);
    }
}

/* A flat screen at zoom 0.5 covers the middle of each eye: green there, black
 * (not the clear colour) in the corners. */
static void flat_corners_black(void **state)
{
    (void)state;
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, 43.f, EYE_ASPECT, 0.5f, 16.0 / 9.0, &g);
    const xrd_draw_params dp = {.proj = XRD_PROJ_FLAT, .layout = XRD_LAYOUT_MONO, .geom = g};
    draw_dp(g_flat_tex, XRD_QUAT_IDENTITY, &dp);
    for (int eye = 0; eye < 2; eye++) {
        int x0 = eye * (W / 2);
        expect_px(x0 + W / 4, H / 2, 0, 255, 0);
        expect_px(x0 + 1, 1, 0, 0, 0);
        expect_px(x0 + W / 2 - 2, 1, 0, 0, 0);
        expect_px(x0 + 1, H - 2, 0, 0, 0);
        expect_px(x0 + W / 2 - 2, H - 2, 0, 0, 0);
    }
}

/* The sharpen pass (its own ES program) and the dither draw without a GL error. */
static void sharpen_and_dither_draw(void **state)
{
    (void)state;
    xrd_renderer_set_sharpen(g_r, 0.5f);
    xrd_renderer_set_dither(g_r, true);
    draw_hequirect(g_tex, XRD_QUAT_IDENTITY);
    xrd_renderer_set_sharpen(g_r, 0.f);
    xrd_renderer_set_dither(g_r, false);
}

static void depth_refused_on_es(void **state)
{
    (void)state;
    uint16_t d[4] = {0, 1000, 2000, 65535};
    char err[128] = "";
    assert_false(xrd_renderer_set_depth(g_r, d, 2, 2, err, sizeof err));
    assert_string_equal(err, "depth 3D needs desktop GL");
    assert_false(xrd_renderer_has_depth(g_r));
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

int main(void)
{
    /* Skip, not fail, where Mesa has no surfaceless ES 3.0 context. */
    void *st = NULL;
    if (setup(&st) != 0) {
        fprintf(stderr, "test_render_gles: no GLES 3.0 context here, skipping\n");
        return 77;
    }
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(is_es),
        cmocka_unit_test(hequirect_centre_colour),
        cmocka_unit_test(sbs_eyes_not_mixed),
        cmocka_unit_test(flat_corners_black),
        cmocka_unit_test(sharpen_and_dither_draw),
        cmocka_unit_test(depth_refused_on_es),
    };
    int rc = cmocka_run_group_tests(tests, NULL, NULL);
    teardown(&st);
    return rc;
}
