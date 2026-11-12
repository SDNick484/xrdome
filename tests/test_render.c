#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include "math/quat.h"
#include "render/projection.h"
#include "render/renderer_gl.h"

#define W 384
#define H 108
#define TEX_W 64
#define TEX_H 32

static EGLDisplay g_dpy;
static EGLContext g_ctx;
static GLuint g_fbo, g_color, g_tex;
static xrd_renderer *g_r;
static float g_th, g_tv;

static int setup(void **state)
{
    (void)state;
    /* epoxy resolves the EXT entry point from the client extension; the core
     * eglGetPlatformDisplay needs an initialised display to pass its version check. */
    g_dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint maj, min;
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, &maj, &min)) {
        fprintf(stderr, "eglInitialize failed: 0x%x\n", eglGetError());
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "eglBindAPI failed: 0x%x\n", eglGetError());
        return -1;
    }
    const EGLint cattr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
    if (g_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed: 0x%x\n", eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx)) {
        fprintf(stderr, "eglMakeCurrent failed: 0x%x\n", eglGetError());
        return -1;
    }

    glGenTextures(1, &g_color);
    glBindTexture(GL_TEXTURE_2D, g_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    char err[512] = "";
    g_r = xrd_renderer_new(err, sizeof err);
    if (!g_r) {
        fprintf(stderr, "renderer_new failed: %s\n", err);
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

static uint8_t g_img[W * H * 4];

static void draw(xrd_quat q, xrd_layout layout, bool swap)
{
    float rot[9];
    xrd_quat_to_mat3(q, rot);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, W, H);
    glClearColor(0.f, 1.f, 0.f, 1.f); /* green: any uncovered pixel fails */
    glClear(GL_COLOR_BUFFER_BIT);
    xrd_renderer_draw(g_r, g_tex, rot, g_th, g_tv, layout, swap, W, H);
    assert_int_equal(glGetError(), GL_NO_ERROR);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g_img);
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

static void expect_px(int x, int y, int r, int g, int b)
{
    const uint8_t *p = &g_img[(y * W + x) * 4];
    int want[3] = {r, g, b};
    for (int c = 0; c < 3; c++)
        if (p[c] < want[c] - 2 || p[c] > want[c] + 2) {
            fail_msg("pixel (%d,%d) = (%d,%d,%d), want (%d,%d,%d)", x, y,
                     p[0], p[1], p[2], r, g, b);
        }
}

static void identity_left_is_red(void **state)
{
    (void)state;
    draw(XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false);
    expect_px(96, 54, 255, 0, 0);
    expect_px(288, 54, 0, 0, 255);
}

static void swap_eyes(void **state)
{
    (void)state;
    draw(XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, true);
    expect_px(96, 54, 0, 0, 255);
    expect_px(288, 54, 255, 0, 0);
}

static void layout_rl(void **state)
{
    (void)state;
    draw(XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_RL, false);
    expect_px(96, 54, 0, 0, 255);
    expect_px(288, 54, 255, 0, 0);
}

static void behind_is_black(void **state)
{
    (void)state;
    draw(xrd_quat_from_ypr_deg(180, 0, 0), XRD_LAYOUT_SBS_LR, false);
    expect_px(96, 54, 0, 0, 0);
    expect_px(288, 54, 0, 0, 0);
}

static void no_seam_bleed(void **state)
{
    (void)state;
    draw(xrd_quat_from_ypr_deg(-89, 0, 0), XRD_LAYOUT_SBS_LR, false);
    int blue_max = 0, red_seen = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W / 2; x++) {
            const uint8_t *p = &g_img[(y * W + x) * 4];
            if (p[2] > blue_max)
                blue_max = p[2];
            if (p[0] > 200)
                red_seen++;
        }
    assert_int_equal(blue_max, 0);
    assert_true(red_seen > 0); /* the test really reached the seam region */
}

static void no_gl_error(void **state)
{
    (void)state;
    draw(XRD_QUAT_IDENTITY, XRD_LAYOUT_MONO, false);
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
        cmocka_unit_test(identity_left_is_red),
        cmocka_unit_test(swap_eyes),
        cmocka_unit_test(layout_rl),
        cmocka_unit_test(behind_is_black),
        cmocka_unit_test(no_seam_bleed),
        cmocka_unit_test(no_gl_error),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}
