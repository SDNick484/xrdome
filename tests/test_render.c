#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
static GLuint g_fbo, g_color, g_tex, g_quad_tex;
static xrd_renderer *g_r;
static float g_th, g_tv;

/* Colour of quadrant (eye, right half?, top half?) of the quadrant texture. */
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

/* RGBA8 texture with GL_LINEAR filtering and clamp-to-edge, as the renderer's
 * contract asks. px is w * h RGBA, row 0 = v = 0 (the image bottom). */
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

    /* Quadrant texture: each eye half has four distinct colours. Row 0 is v = 0
     * (image bottom); rows >= TEX_H/2 are the image top. */
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
    glGenTextures(1, &g_quad_tex);
    glBindTexture(GL_TEXTURE_2D, g_quad_tex);
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

/* Draws with whatever content FOV the renderer currently has. */
static void draw_tex_fov(GLuint tex, xrd_quat q, xrd_layout layout, bool swap, bool hostile)
{
    float rot[9];
    xrd_quat_to_mat3(q, rot);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, W, H);
    glClearColor(0.f, 1.f, 0.f, 1.f); /* green: any uncovered pixel fails */
    glClear(GL_COLOR_BUFFER_BIT);
    if (hostile) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 1, 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_ZERO);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT_AND_BACK);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    }
    const xrd_draw_params dp = {.proj = XRD_PROJ_HEQUIRECT, .layout = layout, .swap_eyes = swap,
                                .geom = {.tan_h = g_th, .tan_v = g_tv}, .trilinear = false};
    xrd_renderer_draw(g_r, tex, rot, &dp, W, H);
    assert_int_equal(glGetError(), GL_NO_ERROR);
    if (hostile) {
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g_img);
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

/* Existing tests: the default (180 x 180) content FOV. */
static void draw_tex(GLuint tex, xrd_quat q, xrd_layout layout, bool swap, bool hostile)
{
    xrd_renderer_set_content_fov(g_r, 180.f, 180.f);
    draw_tex_fov(tex, q, layout, swap, hostile);
}

static void draw(xrd_quat q, xrd_layout layout, bool swap)
{
    draw_tex(g_tex, q, layout, swap, false);
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

/* Looking 89 degrees right, the left eye samples the right edge of its
 * sub-image, next to the right eye's blue; looking 89 degrees left, the right
 * eye samples the left edge of its sub-image, next to the left eye's red.
 * Neither may show any of the other eye's colour. */
static void check_no_seam_bleed(void)
{
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

    draw(xrd_quat_from_ypr_deg(89, 0, 0), XRD_LAYOUT_SBS_LR, false);
    int red_max = 0, blue_seen = 0;
    for (int y = 0; y < H; y++)
        for (int x = W / 2; x < W; x++) {
            const uint8_t *p = &g_img[(y * W + x) * 4];
            if (p[0] > red_max)
                red_max = p[0];
            if (p[2] > 200)
                blue_seen++;
        }
    assert_int_equal(red_max, 0);
    assert_true(blue_seen > 0);
}

static void no_seam_bleed(void **state)
{
    (void)state;
    check_no_seam_bleed();
}

static void draw_ignores_caller_state(void **state)
{
    (void)state;
    draw_tex(g_tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, true);
    expect_px(96, 54, 255, 0, 0);
    expect_px(288, 54, 0, 0, 255);
}

/* Pixel (dx, y) of each eye viewport must show quadrant (right, top) of that
 * eye's image. The exact centre sits on the quadrant boundaries (a 50/50
 * blend), so tests probe the boundary-free axis at the centre and the other
 * axis near the viewport edge, which stays at least 2 texels from a boundary. */
static void expect_quadrant(int dx, int y, int right, int top)
{
    for (int eye = 0; eye < 2; eye++) {
        const uint8_t *c = quad_colour(eye, right, top);
        expect_px(eye * (W / 2) + dx, y, c[0], c[1], c[2]);
    }
}

static void yaw_left_samples_left(void **state)
{
    (void)state;
    draw_tex(g_quad_tex, xrd_quat_from_ypr_deg(30, 0, 0), XRD_LAYOUT_SBS_LR, false, false);
    expect_quadrant(W / 4, H - 1, 0, 1);
    expect_quadrant(W / 4, 0, 0, 0);
}

static void yaw_right_samples_right(void **state)
{
    (void)state;
    draw_tex(g_quad_tex, xrd_quat_from_ypr_deg(-30, 0, 0), XRD_LAYOUT_SBS_LR, false, false);
    expect_quadrant(W / 4, H - 1, 1, 1);
    expect_quadrant(W / 4, 0, 1, 0);
}

static void pitch_up_samples_top(void **state)
{
    (void)state;
    draw_tex(g_quad_tex, xrd_quat_from_ypr_deg(0, 20, 0), XRD_LAYOUT_SBS_LR, false, false);
    expect_quadrant(4, H / 2, 0, 1);
    expect_quadrant(W / 2 - 5, H / 2, 1, 1);
}

static void pitch_down_samples_bottom(void **state)
{
    (void)state;
    draw_tex(g_quad_tex, xrd_quat_from_ypr_deg(0, -20, 0), XRD_LAYOUT_SBS_LR, false, false);
    expect_quadrant(4, H / 2, 0, 0);
    expect_quadrant(W / 2 - 5, H / 2, 1, 0);
}

static void draw_fov(GLuint tex, float hfov, float vfov, xrd_quat q)
{
    xrd_renderer_set_content_fov(g_r, hfov, vfov);
    draw_tex_fov(tex, q, XRD_LAYOUT_SBS_LR, false, false);
    xrd_renderer_set_content_fov(g_r, 180.f, 180.f);
}

static void fov90_identity_still_red_blue(void **state)
{
    (void)state;
    draw_fov(g_tex, 90.f, 90.f, XRD_QUAT_IDENTITY);
    expect_px(96, 54, 255, 0, 0);
    expect_px(288, 54, 0, 0, 255);
}

static void fov90_outside_yaw_is_black(void **state)
{
    (void)state;
    /* looking 50 degrees right: the centre is past the content edge at 45 */
    draw_fov(g_tex, 90.f, 90.f, xrd_quat_from_ypr_deg(-50, 0, 0));
    expect_px(96, 54, 0, 0, 0);
    expect_px(288, 54, 0, 0, 0);
    draw_fov(g_tex, 90.f, 90.f, xrd_quat_from_ypr_deg(50, 0, 0));
    expect_px(96, 54, 0, 0, 0);
    expect_px(288, 54, 0, 0, 0);
}

static void fov90_inside_yaw_is_not_black(void **state)
{
    (void)state;
    draw_fov(g_quad_tex, 90.f, 90.f, xrd_quat_from_ypr_deg(-40, 0, 0));
    /* looking right of centre: right half of each eye image, still lit */
    expect_quadrant(W / 4, H - 1, 1, 1);
    expect_quadrant(W / 4, 0, 1, 0);
}

static void fov90_pitch_outside_is_black(void **state)
{
    (void)state;
    draw_fov(g_quad_tex, 90.f, 90.f, xrd_quat_from_ypr_deg(0, 50, 0));
    expect_px(96, 54, 0, 0, 0);
    expect_px(288, 54, 0, 0, 0);
    draw_fov(g_quad_tex, 90.f, 90.f, xrd_quat_from_ypr_deg(0, -50, 0));
    expect_px(96, 54, 0, 0, 0);
    expect_px(288, 54, 0, 0, 0);
}

static void fov90_edge_splits_the_view(void **state)
{
    (void)state;
    /* Looking 40 degrees right: the view spans about 21 to 59 degrees, the
     * content ends at 45, so the left of each eye is lit and the right is black. */
    draw_fov(g_tex, 90.f, 90.f, xrd_quat_from_ypr_deg(-40, 0, 0));
    for (int eye = 0; eye < 2; eye++) {
        const uint8_t *lit = &g_img[(54 * W + eye * (W / 2) + 4) * 4];
        const uint8_t *dark = &g_img[(54 * W + eye * (W / 2) + W / 2 - 5) * 4];
        assert_true(lit[0] + lit[2] > 200);
        assert_int_equal(dark[0] + dark[1] + dark[2], 0);
    }
}

static void fov180x90_rejects_only_vertically(void **state)
{
    (void)state;
    draw_fov(g_tex, 180.f, 90.f, xrd_quat_from_ypr_deg(-80, 0, 0));
    expect_px(96, 54, 255, 0, 0);
    expect_px(288, 54, 0, 0, 255);
    draw_fov(g_tex, 180.f, 90.f, xrd_quat_from_ypr_deg(0, 50, 0));
    expect_px(96, 54, 0, 0, 0);
    expect_px(288, 54, 0, 0, 0);
}

/* Restores the renderer's defaults after a test that changed them. */
static int reset_view(void **state)
{
    (void)state;
    xrd_renderer_set_filter(g_r, XRD_FILTER_BILINEAR);
    xrd_renderer_set_convergence(g_r, 0.f);
    xrd_renderer_set_content_fov(g_r, 180.f, 180.f);
    xrd_renderer_set_dither(g_r, false);
    return 0;
}

/* ---- convergence ---- */

/* Each eye's sub-image is LINE_EW texels wide (odd, so one texel column sits
 * exactly on u = 0.5), black with one white column at its centre. */
#define LINE_EW 65
#define LINE_H 16

static GLuint make_line_tex(void)
{
    static uint8_t px[2 * LINE_EW * LINE_H * 4];
    for (int y = 0; y < LINE_H; y++)
        for (int x = 0; x < 2 * LINE_EW; x++) {
            uint8_t *p = &px[(y * 2 * LINE_EW + x) * 4];
            uint8_t v = (x % LINE_EW) == LINE_EW / 2 ? 255 : 0;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    return make_tex(2 * LINE_EW, LINE_H, px);
}

/* Red-weighted centroid of the middle row of viewport eye, in pixels from
 * that viewport's left edge (pixel i covers [i, i + 1), so the centre of a
 * 192-pixel viewport is 96.0 and pixel 95's centre is 95.5). */
static double line_centroid_row(int eye, int y)
{
    double sum = 0, wsum = 0;
    for (int x = 0; x < W / 2; x++) {
        double v = g_img[(y * W + eye * (W / 2) + x) * 4];
        sum += v * (x + 0.5);
        wsum += v;
    }
    assert_true(wsum > 0);
    return sum / wsum;
}

static double line_centroid(int eye)
{
    return line_centroid_row(eye, H / 2);
}

/* Where the content straight ahead (lon 0) lands when the eye looks yaw_right
 * degrees to the right of it: tan-space, independent of the renderer. Returns
 * the offset from the viewport centre in pixels (negative = left). */
static double expected_offset_px(double yaw_right_deg)
{
    double t = tan(-yaw_right_deg * 3.14159265358979323846 / 180.0);
    return t / g_th * (W / 4.0); /* half a viewport is W/4 pixels */
}

static void draw_converged(GLuint tex, float conv_deg)
{
    xrd_renderer_set_convergence(g_r, conv_deg);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, false);
}

static void convergence_zero_centres_line(void **state)
{
    (void)state;
    GLuint tex = make_line_tex();
    draw_converged(tex, 0.f);
    double centre = W / 4.0;
    for (int eye = 0; eye < 2; eye++) {
        double c = line_centroid(eye);
        if (fabs(c - centre) > 1.5)
            fail_msg("eye %d: line at %.2f px, want %.2f", eye, c, centre);
    }
    glDeleteTextures(1, &tex);
}

/* Positive convergence toes the eyes in: the left eye looks right by half the
 * angle, so straight-ahead content appears LEFT of the left viewport's centre;
 * the right eye looks left, so it appears RIGHT of the right viewport's
 * centre. Negative convergence reverses both. */
static void check_convergence(float conv_deg)
{
    GLuint tex = make_line_tex();
    draw_converged(tex, conv_deg);
    double centre = W / 4.0;
    double left = line_centroid(0) - centre, right = line_centroid(1) - centre;
    /* left eye looks right by conv/2, right eye looks left by conv/2 */
    double want_left = expected_offset_px(conv_deg / 2.0);
    double want_right = expected_offset_px(-conv_deg / 2.0);
    if (conv_deg > 0) {
        assert_true(want_left < -3.0 && want_right > 3.0); /* the expectation itself */
        assert_true(left < 0.0);
        assert_true(right > 0.0);
    } else {
        assert_true(want_left > 3.0 && want_right < -3.0);
        assert_true(left > 0.0);
        assert_true(right < 0.0);
    }
    if (fabs(left - want_left) > 1.5 || fabs(right - want_right) > 1.5)
        fail_msg("conv %.1f: offsets left %.2f right %.2f px, want %.2f and %.2f",
                 (double)conv_deg, left, right, want_left, want_right);
    if (fabs(left + right) > 1.0)
        fail_msg("conv %.1f: unequal shifts %.2f and %.2f px", (double)conv_deg, left, right);
    glDeleteTextures(1, &tex);
}

static void convergence_positive_separates_left_left(void **state)
{
    (void)state;
    check_convergence(4.f);
}

static void convergence_negative_reverses(void **state)
{
    (void)state;
    check_convergence(-4.f);
}

/* A display property: it follows the viewport, not the sub-image, so mono and
 * swapped eyes shift the same way. */
static void convergence_applies_in_mono_and_swap(void **state)
{
    (void)state;
    GLuint tex = make_line_tex();
    xrd_renderer_set_convergence(g_r, 4.f);
    double centre = W / 4.0, want = expected_offset_px(2.0);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, true, false);
    assert_true(fabs(line_centroid(0) - centre - want) <= 1.5);
    assert_true(fabs(line_centroid(1) - centre + want) <= 1.5);
    glDeleteTextures(1, &tex);

    /* mono: one centred line across the whole texture (odd width) */
    static uint8_t px[LINE_EW * LINE_H * 4];
    for (int y = 0; y < LINE_H; y++)
        for (int x = 0; x < LINE_EW; x++) {
            uint8_t *p = &px[(y * LINE_EW + x) * 4];
            p[0] = p[1] = p[2] = x == LINE_EW / 2 ? 255 : 0;
            p[3] = 255;
        }
    tex = make_tex(LINE_EW, LINE_H, px);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_MONO, false, false);
    assert_true(fabs(line_centroid(0) - centre - want) <= 1.5);
    assert_true(fabs(line_centroid(1) - centre + want) <= 1.5);
    glDeleteTextures(1, &tex);
}

/* Convergence with the head pitched up 30 degrees: the eye yaw is applied in
 * head space after the view rotation (rot * eye_yaw), so the meridian under
 * the line still projects to a vertical line in each viewport, shifted by the
 * same tan-space amount as with a level head, in opposite directions. Applying
 * the yaw in world space first (eye_yaw * rot) would tilt the line and shrink
 * the shift by cos(30). */
static void check_convergence_pitched(float conv_deg)
{
    GLuint tex = make_line_tex();
    xrd_renderer_set_convergence(g_r, conv_deg);
    draw_tex(tex, xrd_quat_from_ypr_deg(0, 30, 0), XRD_LAYOUT_SBS_LR, false, false);
    double centre = W / 4.0;
    double want_left = expected_offset_px(conv_deg / 2.0);
    double want_right = expected_offset_px(-conv_deg / 2.0);
    const int rows[] = {H / 8, H / 2, H - 1 - H / 8};
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        double left = line_centroid_row(0, rows[i]) - centre;
        double right = line_centroid_row(1, rows[i]) - centre;
        if (fabs(left - want_left) > 0.75 || fabs(right - want_right) > 0.75)
            fail_msg("pitched, conv %.1f, row %d: offsets left %.2f right %.2f px, "
                     "want %.2f and %.2f", (double)conv_deg, rows[i], left, right,
                     want_left, want_right);
        if (fabs(left + right) > 0.75)
            fail_msg("pitched, conv %.1f, row %d: unequal shifts %.2f and %.2f px",
                     (double)conv_deg, rows[i], left, right);
    }
    glDeleteTextures(1, &tex);
}

static void convergence_pitched_shifts_horizontally(void **state)
{
    (void)state;
    check_convergence_pitched(4.f);
    check_convergence_pitched(-4.f);
}

/* ---- bicubic (Catmull-Rom) ---- */

#define STEP_EW 32
#define STEP_H 16
#define STEP_LO 50
#define STEP_HI 200

/* Each eye's sub-image: grey STEP_LO left of its centre, STEP_HI right of it,
 * the same in every row. */
static uint8_t step_value(int x_in_eye)
{
    return x_in_eye < STEP_EW / 2 ? STEP_LO : STEP_HI;
}

static GLuint make_step_tex(void)
{
    static uint8_t px[2 * STEP_EW * STEP_H * 4];
    for (int y = 0; y < STEP_H; y++)
        for (int x = 0; x < 2 * STEP_EW; x++) {
            uint8_t *p = &px[(y * 2 * STEP_EW + x) * 4];
            p[0] = p[1] = p[2] = step_value(x % STEP_EW);
            p[3] = 255;
        }
    return make_tex(2 * STEP_EW, STEP_H, px);
}

/* Catmull-Rom of one row of n texels at texel-space position p (texel i is
 * centred on i + 0.5), clamping indices to the row (clamp to edge). */
static double catmull_rom(const double *row, int n, double p)
{
    double s = p - 0.5;
    int i1 = (int)floor(s);
    double f = s - i1;
    double w[4] = {
        0.5 * (-f * f * f + 2 * f * f - f),
        0.5 * (3 * f * f * f - 5 * f * f + 2),
        0.5 * (-3 * f * f * f + 4 * f * f + f),
        0.5 * (f * f * f - f * f),
    };
    double sum = 0;
    for (int k = 0; k < 4; k++) {
        int i = i1 - 1 + k;
        i = i < 0 ? 0 : (i > n - 1 ? n - 1 : i);
        sum += w[k] * row[i];
    }
    return sum;
}

/* Reference value of viewport pixel (x, y) for the step texture, identity
 * view, 180 x 180 content: the eye-pass mapping in double precision, the
 * half-texel seam clamp, then Catmull-Rom across the eye's sub-image. */
static double step_reference(int x, int y)
{
    double ndc_x = (x + 0.5) / (W / 4.0) - 1.0, ndc_y = (y + 0.5) / (H / 2.0) - 1.0;
    double dx = ndc_x * g_th, dy = ndc_y * g_tv, dz = -1.0;
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    dx /= len;
    dz /= len;
    double u = atan2(dx, -dz) / 3.14159265358979323846 + 0.5;
    double m = 0.5 / STEP_EW;
    u = u < m ? m : (u > 1.0 - m ? 1.0 - m : u);
    double row[STEP_EW];
    for (int i = 0; i < STEP_EW; i++)
        row[i] = step_value(i);
    double v = catmull_rom(row, STEP_EW, u * STEP_EW);
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static void bicubic_matches_catmull_rom_reference(void **state)
{
    (void)state;
    GLuint tex = make_step_tex();
    xrd_renderer_set_filter(g_r, XRD_FILTER_BICUBIC);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, false);
    int y = H / 2;
    double undershoot = 255, overshoot = 0;
    for (int eye = 0; eye < 2; eye++)
        for (int x = 0; x < W / 2; x++) {
            double want = step_reference(x, y);
            if (want < undershoot)
                undershoot = want;
            if (want > overshoot)
                overshoot = want;
            const uint8_t *p = &g_img[(y * W + eye * (W / 2) + x) * 4];
            for (int c = 0; c < 3; c++)
                if (fabs(p[c] - want) > 3.0)
                    fail_msg("eye %d pixel %d channel %d = %d, Catmull-Rom reference %.2f",
                             eye, x, c, p[c], want);
        }
    /* The reference really rings around the edge, so it is not bilinear. */
    assert_true(undershoot < STEP_LO - 5);
    assert_true(overshoot > STEP_HI + 5);
    glDeleteTextures(1, &tex);
}

/* Transposed step: each eye's sub-image is STEP_LO in its lower half and
 * STEP_HI in its upper half, the same in every column, so the vertical taps of
 * the Catmull-Rom filter decide every pixel of a column. */
#define VSTEP_EW 8
#define VSTEP_H 32

static uint8_t vstep_value(int y)
{
    return y < VSTEP_H / 2 ? STEP_LO : STEP_HI;
}

static GLuint make_vstep_tex(void)
{
    static uint8_t px[2 * VSTEP_EW * VSTEP_H * 4];
    for (int y = 0; y < VSTEP_H; y++)
        for (int x = 0; x < 2 * VSTEP_EW; x++) {
            uint8_t *p = &px[(y * 2 * VSTEP_EW + x) * 4];
            p[0] = p[1] = p[2] = vstep_value(y);
            p[3] = 255;
        }
    return make_tex(2 * VSTEP_EW, VSTEP_H, px);
}

/* Reference value of viewport pixel (x, y) for the transposed step texture,
 * identity view, 180 x 180 content: v from the latitude, the half-texel clamp
 * on v, then Catmull-Rom down the column. */
static double vstep_reference(int x, int y)
{
    double ndc_x = (x + 0.5) / (W / 4.0) - 1.0, ndc_y = (y + 0.5) / (H / 2.0) - 1.0;
    double dx = ndc_x * g_th, dy = ndc_y * g_tv, dz = -1.0;
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    double v = asin(dy / len) / 3.14159265358979323846 + 0.5;
    double m = 0.5 / VSTEP_H;
    v = v < m ? m : (v > 1.0 - m ? 1.0 - m : v);
    double col[VSTEP_H];
    for (int i = 0; i < VSTEP_H; i++)
        col[i] = vstep_value(i);
    double r = catmull_rom(col, VSTEP_H, v * VSTEP_H);
    return r < 0 ? 0 : (r > 255 ? 255 : r);
}

static void bicubic_vertical_matches_catmull_rom_reference(void **state)
{
    (void)state;
    GLuint tex = make_vstep_tex();
    xrd_renderer_set_filter(g_r, XRD_FILTER_BICUBIC);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, false);
    int x = W / 4;
    double undershoot = 255, overshoot = 0;
    for (int eye = 0; eye < 2; eye++)
        for (int y = 0; y < H; y++) {
            double want = vstep_reference(x, y);
            if (want < undershoot)
                undershoot = want;
            if (want > overshoot)
                overshoot = want;
            const uint8_t *p = &g_img[(y * W + eye * (W / 2) + x) * 4];
            for (int c = 0; c < 3; c++)
                if (fabs(p[c] - want) > 3.0)
                    fail_msg("eye %d row %d channel %d = %d, Catmull-Rom reference %.2f",
                             eye, y, c, p[c], want);
        }
    /* The reference rings around the edge, so bilinear would not pass. */
    assert_true(undershoot < STEP_LO - 5);
    assert_true(overshoot > STEP_HI + 5);
    glDeleteTextures(1, &tex);
}

static void bicubic_differs_from_bilinear_near_edge(void **state)
{
    (void)state;
    GLuint tex = make_step_tex();
    static uint8_t lin[W * H * 4];
    xrd_renderer_set_filter(g_r, XRD_FILTER_BILINEAR);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, false);
    memcpy(lin, g_img, sizeof lin);
    xrd_renderer_set_filter(g_r, XRD_FILTER_BICUBIC);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, false);
    int y = H / 2, max_diff = 0, at = -1;
    for (int x = 0; x < W / 2; x++) {
        int d = abs((int)g_img[(y * W + x) * 4] - (int)lin[(y * W + x) * 4]);
        if (d > max_diff) {
            max_diff = d;
            at = x;
        }
    }
    assert_true(max_diff >= 6);
    /* ...and that difference is near the edge (the viewport centre). */
    assert_true(abs(at - W / 4) < W / 8);
    glDeleteTextures(1, &tex);
}

static void bicubic_keeps_flat_colour(void **state)
{
    (void)state;
    static uint8_t px[TEX_W * TEX_H * 4];
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        px[i * 4 + 0] = 37;
        px[i * 4 + 1] = 140;
        px[i * 4 + 2] = 201;
        px[i * 4 + 3] = 255;
    }
    GLuint tex = make_tex(TEX_W, TEX_H, px);
    xrd_renderer_set_filter(g_r, XRD_FILTER_BICUBIC);
    const xrd_quat views[] = {XRD_QUAT_IDENTITY, xrd_quat_from_ypr_deg(-60, 25, 0),
                              xrd_quat_from_ypr_deg(80, -70, 10)};
    for (size_t v = 0; v < sizeof views / sizeof views[0]; v++) {
        draw_tex(tex, views[v], XRD_LAYOUT_SBS_LR, false, false);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const uint8_t *p = &g_img[(y * W + x) * 4];
                if (p[0] + p[1] + p[2] == 0)
                    continue; /* behind the viewer: black by design */
                if (abs(p[0] - 37) > 1 || abs(p[1] - 140) > 1 || abs(p[2] - 201) > 1)
                    fail_msg("view %zu pixel (%d,%d) = (%d,%d,%d), want (37,140,201)", v, x, y,
                             p[0], p[1], p[2]);
            }
    }
    glDeleteTextures(1, &tex);
}

static void bicubic_no_seam_bleed(void **state)
{
    (void)state;
    xrd_renderer_set_filter(g_r, XRD_FILTER_BICUBIC);
    check_no_seam_bleed();
}

/* The red/blue texture's eye centres are flat, so bicubic shows the same. */
static void bicubic_identity_left_is_red(void **state)
{
    (void)state;
    xrd_renderer_set_filter(g_r, XRD_FILTER_BICUBIC);
    draw(XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false);
    expect_px(96, 54, 255, 0, 0);
    expect_px(288, 54, 0, 0, 255);
}

/* ---- 10-bit texture and dithering ---- */

/* RGB10_A2 texture of w x h texels, every texel (r, g, b, a) in 10-bit units
 * (a in 2-bit units), GL_LINEAR and clamp-to-edge. */
static GLuint make_tex10(int w, int h, unsigned r, unsigned g, unsigned b, unsigned a)
{
    uint32_t *px = malloc((size_t)w * h * sizeof *px);
    assert_non_null(px);
    for (int i = 0; i < w * h; i++)
        px[i] = r | g << 10 | b << 20 | (uint32_t)a << 30;
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_INT_2_10_10_10_REV, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(px);
    assert_int_equal(glGetError(), GL_NO_ERROR);
    return t;
}

static GLuint make_flat_tex(uint8_t r, uint8_t g, uint8_t b)
{
    static uint8_t px[TEX_W * TEX_H * 4];
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = 255;
    }
    return make_tex(TEX_W, TEX_H, px);
}

/* A 32 x 32 block centred in the left eye viewport. */
#define BLK 32
#define BLK_X0 (W / 4 - BLK / 2)
#define BLK_Y0 (H / 2 - BLK / 2)

/* Grey 514/1023 = 128.12 in 8-bit units, between two 8-bit levels. Its alpha
 * is 0, so the test also shows the eye pass writes alpha 1 itself. */
#define GREY10 514u

static void tenbit_precision_survives_dither(void **state)
{
    (void)state;
    GLuint tex = make_tex10(TEX_W, TEX_H, GREY10, GREY10, GREY10, 0);
    const double want_mean = GREY10 * 255.0 / 1023.0;
    xrd_renderer_set_dither(g_r, true);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, false);
    for (int c = 0; c < 3; c++) {
        int n128 = 0, n129 = 0;
        double sum = 0;
        for (int y = BLK_Y0; y < BLK_Y0 + BLK; y++)
            for (int x = BLK_X0; x < BLK_X0 + BLK; x++) {
                const uint8_t *p = &g_img[(y * W + x) * 4];
                if (p[c] != 128 && p[c] != 129)
                    fail_msg("dither on: pixel (%d,%d) channel %d = %d, want 128 or 129",
                             x, y, c, p[c]);
                n128 += p[c] == 128;
                n129 += p[c] == 129;
                sum += p[c];
                assert_int_equal(p[3], 255);
            }
        double mean = sum / (BLK * BLK);
        if (n128 == 0 || n129 == 0 || fabs(mean - want_mean) > 0.1)
            fail_msg("dither on: channel %d: %d x 128, %d x 129, mean %.3f, want %.3f", c,
                     n128, n129, mean, want_mean);
    }

    xrd_renderer_set_dither(g_r, false);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, false);
    for (int y = BLK_Y0; y < BLK_Y0 + BLK; y++)
        for (int x = BLK_X0; x < BLK_X0 + BLK; x++) {
            const uint8_t *p = &g_img[(y * W + x) * 4];
            if (p[0] != 128 || p[1] != 128 || p[2] != 128 || p[3] != 255)
                fail_msg("dither off: pixel (%d,%d) = (%d,%d,%d,%d), want (128,128,128,255)",
                         x, y, p[0], p[1], p[2], p[3]);
        }
    glDeleteTextures(1, &tex);
}

/* Dithering moves an exact 8-bit value by at most one level, everywhere, with
 * either filter. */
static void dither_is_bounded(void **state)
{
    (void)state;
    GLuint tex = make_flat_tex(128, 128, 128);
    xrd_renderer_set_dither(g_r, true);
    const xrd_filter filters[] = {XRD_FILTER_BILINEAR, XRD_FILTER_BICUBIC};
    for (size_t f = 0; f < 2; f++) {
        xrd_renderer_set_filter(g_r, filters[f]);
        draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_SBS_LR, false, false);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const uint8_t *p = &g_img[(y * W + x) * 4];
                for (int c = 0; c < 3; c++)
                    if (abs(p[c] - 128) > 1)
                        fail_msg("filter %zu pixel (%d,%d) channel %d = %d, want 128 +- 1",
                                 f, x, y, c, p[c]);
            }
    }
    glDeleteTextures(1, &tex);
}

static void expect_all_black(const char *what)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const uint8_t *p = &g_img[(y * W + x) * 4];
            if (p[0] || p[1] || p[2] || p[3] != 255)
                fail_msg("%s: pixel (%d,%d) = (%d,%d,%d,%d), want (0,0,0,255)", what, x, y,
                         p[0], p[1], p[2], p[3]);
        }
}

/* The "behind the viewer" and "outside the content" blacks are not dithered. */
static void dither_keeps_black_black(void **state)
{
    (void)state;
    GLuint tex = make_flat_tex(128, 128, 128);
    xrd_renderer_set_dither(g_r, true);
    draw_tex(tex, xrd_quat_from_ypr_deg(180, 0, 0), XRD_LAYOUT_SBS_LR, false, false);
    expect_all_black("behind");
    /* looking 70 degrees right: the view spans about 51 to 89, content ends at 45 */
    draw_fov(tex, 90.f, 90.f, xrd_quat_from_ypr_deg(-70, 0, 0));
    expect_all_black("outside the content");
    glDeleteTextures(1, &tex);
}

/* ---- flat projection and the two-axis eye split ---- */

/* Viture Pro XR: the eye viewport is W/2 x H = 192 x 108, 16:9. */
#define EYE_ASPECT ((float)(W / 2) / (float)H)

static void flat_geom(xrd_zoom_mode mode, float zoom, double picture_aspect, xrd_view_geom *g)
{
    xrd_view_geometry(XRD_PROJ_FLAT, mode, 43.f, EYE_ASPECT, zoom, picture_aspect, g);
}

/* Draws dp into the test framebuffer, cleared to magenta first so that a
 * pixel the pass leaves alone cannot pass for green, red, blue or black. */
static void draw_dp(GLuint tex, xrd_quat q, const xrd_draw_params *dp)
{
    float rot[9];
    xrd_quat_to_mat3(q, rot);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, W, H);
    glClearColor(1.f, 0.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    xrd_renderer_draw(g_r, tex, rot, dp, W, H);
    assert_int_equal(glGetError(), GL_NO_ERROR);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g_img);
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

static void draw_flat(GLuint tex, xrd_quat q, xrd_layout layout, bool swap, const xrd_view_geom *g)
{
    const xrd_draw_params dp = {.proj = XRD_PROJ_FLAT, .layout = layout, .swap_eyes = swap,
                                .geom = *g, .trilinear = false};
    draw_dp(tex, q, &dp);
}

static const uint8_t *px_at(int x, int y)
{
    return &g_img[(y * W + x) * 4];
}

/* 16:9 picture, 384 x 216 texels: solid green with a white top row (row
 * FLAT_H - 1, since row 0 is the image bottom). */
#define FLAT_W 384
#define FLAT_H 216

static GLuint make_flat_green_tex(void)
{
    uint8_t *px = malloc((size_t)FLAT_W * FLAT_H * 4);
    assert_non_null(px);
    for (int y = 0; y < FLAT_H; y++)
        for (int x = 0; x < FLAT_W; x++) {
            uint8_t *p = &px[(y * FLAT_W + x) * 4];
            bool top = y == FLAT_H - 1;
            p[0] = top ? 255 : 0;
            p[1] = 255;
            p[2] = top ? 255 : 0;
            p[3] = 255;
        }
    GLuint t = make_tex(FLAT_W, FLAT_H, px);
    free(px);
    return t;
}

static void flat_mono_centre_edge_and_outside(void **state)
{
    (void)state;
    GLuint tex = make_flat_green_tex();
    xrd_view_geom g;

    /* zoom 1: the 16:9 picture fills the 16:9 eye exactly */
    flat_geom(XRD_ZOOM_SCREEN, 1.f, 16.0 / 9.0, &g);
    draw_flat(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_MONO, false, &g);
    expect_px(W / 4, H / 2, 0, 255, 0);
    expect_px(W / 2 + W / 4, H / 2, 0, 255, 0);
    /* The white row lies on a single output row, the same one across the eye:
     * a flat screen's top edge does not bow (a curved mapping would bend it by
     * several rows between the centre and the sides). */
    const int cols[] = {W / 2 / 10, W / 4, W / 2 * 9 / 10};
    int row_of[3];
    for (int eye = 0; eye < 2; eye++)
        for (int i = 0; i < 3; i++) {
            int n = 0;
            for (int y = 0; y < H; y++)
                if (px_at(eye * (W / 2) + cols[i], y)[0] > 32) {
                    n++;
                    row_of[i] = y;
                }
            if (n != 1)
                fail_msg("eye %d column %d: white on %d rows, want 1", eye, cols[i], n);
            if (row_of[i] != row_of[0])
                fail_msg("eye %d: white row %d at column %d, %d at column %d", eye, row_of[i],
                         cols[i], row_of[0], cols[0]);
        }
    assert_int_equal(row_of[0], H - 1);

    /* zoom 0.5: the screen covers the middle half of the eye on each axis */
    flat_geom(XRD_ZOOM_SCREEN, 0.5f, 16.0 / 9.0, &g);
    draw_flat(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_MONO, false, &g);
    for (int eye = 0; eye < 2; eye++) {
        int x0 = eye * (W / 2);
        expect_px(x0 + W / 4, H / 2, 0, 255, 0);
        expect_px(x0 + W / 8 - 2, H / 2, 0, 0, 0);     /* just left of the screen */
        expect_px(x0 + W / 8 + 2, H / 2, 0, 255, 0);   /* just inside it */
        expect_px(x0 + 3 * W / 8 + 2, H / 2, 0, 0, 0); /* just right of it */
        expect_px(x0 + W / 4, H / 4 - 2, 0, 0, 0);     /* just below it */
        expect_px(x0 + W / 4, 3 * H / 4 + 2, 0, 0, 0); /* just above it */
        expect_px(x0 + 2, 2, 0, 0, 0);
    }
    glDeleteTextures(1, &tex);
}

static void flat_behind_is_black(void **state)
{
    (void)state;
    GLuint tex = make_flat_green_tex();
    xrd_view_geom g;
    flat_geom(XRD_ZOOM_SCREEN, 4.f, 16.0 / 9.0, &g);
    draw_flat(tex, xrd_quat_from_ypr_deg(180, 0, 0), XRD_LAYOUT_MONO, false, &g);
    expect_all_black("flat, behind");
    /* looking 90 degrees right: the screen's plane is edge-on */
    draw_flat(tex, xrd_quat_from_ypr_deg(-90, 0, 0), XRD_LAYOUT_MONO, false, &g);
    expect_all_black("flat, beside");
    /* a junk screen (zero extents) draws black, not garbage */
    xrd_view_geom bad = g;
    bad.half_w = 0.f;
    draw_flat(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_MONO, false, &bad);
    expect_all_black("flat, zero-width screen");
    glDeleteTextures(1, &tex);
}

/* A stereo test frame: the first eye's half red, the second's blue. sbs
 * splits columns (left red), tb splits rows (top red: rows >= split, row 0
 * being the image bottom). split is the first texel of the second colour
 * along the split axis, counted from the left or from the top. */
static GLuint make_split_tex(int w, int h, bool tb, int split)
{
    uint8_t *px = malloc((size_t)w * h * 4);
    assert_non_null(px);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t *p = &px[((size_t)y * w + x) * 4];
            /* index along the split axis, from the left or from the top */
            int a = tb ? h - 1 - y : x;
            bool red = a < split;
            p[0] = red ? 255 : 0;
            p[1] = 0;
            p[2] = red ? 0 : 255;
            p[3] = 255;
        }
    GLuint t = make_tex(w, h, px);
    free(px);
    return t;
}

/* 20 points inside a viewport (5 x 4 grid, away from the edges). */
static void expect_viewport_colour(int eye, int r, int g, int b)
{
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) {
            int x = eye * (W / 2) + (int)((0.1 + 0.2 * i) * (W / 2));
            int y = (int)((0.125 + 0.25 * j) * H);
            expect_px(x, y, r, g, b);
        }
}

static void check_flat_stereo_colours(GLuint tex, xrd_layout layout, double frame_aspect)
{
    const xrd_filter filters[] = {XRD_FILTER_BILINEAR, XRD_FILTER_BICUBIC};
    double a = xrd_picture_aspect(layout, xrd_packing_resolve(layout, XRD_PACKING_AUTO,
                                                               frame_aspect), frame_aspect);
    assert_float_equal(a, 16.0 / 9.0, 1e-9); /* the test frames hold 16:9 pictures */
    xrd_view_geom g;
    flat_geom(XRD_ZOOM_SCREEN, 1.f, a, &g);
    for (size_t f = 0; f < 2; f++) {
        xrd_renderer_set_filter(g_r, filters[f]);
        draw_flat(tex, XRD_QUAT_IDENTITY, layout, false, &g);
        expect_viewport_colour(0, 255, 0, 0);
        expect_viewport_colour(1, 0, 0, 255);
        draw_flat(tex, XRD_QUAT_IDENTITY, layout, true, &g);
        expect_viewport_colour(0, 0, 0, 255);
        expect_viewport_colour(1, 255, 0, 0);
    }
}

/* Half SBS: a 16:9 frame holding two squeezed 16:9 pictures. */
static void flat_half_sbs_eyes(void **state)
{
    (void)state;
    GLuint tex = make_split_tex(64, 36, false, 32);
    check_flat_stereo_colours(tex, XRD_LAYOUT_SBS_LR, 64.0 / 36.0);
    glDeleteTextures(1, &tex);
}

/* Full top-bottom: an 8:9 frame holding two full 16:9 pictures. */
static void flat_full_tb_eyes(void **state)
{
    (void)state;
    GLuint tex = make_split_tex(64, 72, true, 36);
    check_flat_stereo_colours(tex, XRD_LAYOUT_TB_LR, 64.0 / 72.0);
    glDeleteTextures(1, &tex);
}

/* Largest value of channel c over viewport eye, and how many pixels there
 * have channel lit > 200 and how many are black. */
static void viewport_stats(int eye, int c, int lit, int *max_c, int *n_lit, int *n_black)
{
    *max_c = 0;
    *n_lit = 0;
    *n_black = 0;
    for (int y = 0; y < H; y++)
        for (int x = eye * (W / 2); x < (eye + 1) * (W / 2); x++) {
            const uint8_t *p = px_at(x, y);
            if (p[c] > *max_c)
                *max_c = p[c];
            *n_lit += p[lit] > 200;
            *n_black += p[0] + p[1] + p[2] == 0;
        }
}

/* Review Focus 4. The view is narrowed 20x (magnify) and turned to the
 * screen's edge on the split axis, so one texel of the frame spans several
 * display pixels and the seam-side edge of an eye's sub-image fills half of
 * a viewport. tb == false: sbs, looking right at the left eye's seam edge and
 * left at the right eye's. tb == true: tb-lr, looking down at the top (left)
 * eye's seam edge and up at the bottom (right) eye's. The middle texel of an
 * odd axis is coloured as the other eye in each check, so taking it into
 * either eye fails. */
static void check_seam_at_size(int w, int h, bool tb)
{
    const xrd_layout layout = tb ? XRD_LAYOUT_TB_LR : XRD_LAYOUT_SBS_LR;
    const xrd_packing packing = tb ? XRD_PACKING_FULL : XRD_PACKING_HALF;
    const double f = (double)w / h;
    xrd_view_geom g;
    flat_geom(XRD_ZOOM_MAGNIFY, 20.f, xrd_picture_aspect(layout, packing, f), &g);
    const double to_edge_deg = atan(tb ? g.half_h : g.half_w) * 180.0 / 3.14159265358979323846;
    const int n = tb ? h : w;
    const xrd_filter filters[] = {XRD_FILTER_BILINEAR, XRD_FILTER_BICUBIC};

    for (int which = 0; which < 2; which++) {
        /* which 0: the left eye (red) at its seam edge, the middle texel blue.
         * which 1: the right eye (blue) at its seam edge, the middle texel red. */
        int split = which == 0 ? n / 2 : (n + 1) / 2;
        GLuint tex = make_split_tex(w, h, tb, split);
        /* sbs: yaw right (negative) to the left eye's u = 1 edge, left for the
         * right eye's u = 0 edge. tb: pitch down (negative) to the top eye's
         * v = 0 edge, up for the bottom eye's v = 1 edge. */
        double ang = which == 0 ? -to_edge_deg : to_edge_deg;
        xrd_quat q = tb ? xrd_quat_from_ypr_deg(0, ang, 0) : xrd_quat_from_ypr_deg(ang, 0, 0);
        for (size_t fi = 0; fi < 2; fi++) {
            xrd_renderer_set_filter(g_r, filters[fi]);
            draw_flat(tex, q, layout, false, &g);
            int eye = which, max_other, n_own, n_black;
            /* the other eye's colour: blue (2) for the left eye, red (0) for the right */
            viewport_stats(eye, which == 0 ? 2 : 0, which == 0 ? 0 : 2, &max_other, &n_own,
                           &n_black);
            if (max_other > 8)
                fail_msg("%dx%d %s, %s, eye %d: other eye's colour up to %d/255", w, h,
                         tb ? "tb-lr" : "sbs-lr", fi ? "Catmull-Rom" : "bilinear", eye,
                         max_other);
            /* the view really straddles the screen's edge */
            if (n_own < W / 2 * H / 4 || n_black < W / 2 * H / 4)
                fail_msg("%dx%d %s eye %d: %d own-colour and %d black pixels; the view "
                         "missed the seam edge", w, h, tb ? "tb" : "sbs", eye, n_own, n_black);
        }
        glDeleteTextures(1, &tex);
    }
}

static void flat_seam_even_and_odd_sizes(void **state)
{
    (void)state;
    check_seam_at_size(1920, 1080, false);
    check_seam_at_size(1919, 1080, false);
    check_seam_at_size(1920, 1081, false);
    check_seam_at_size(1920, 2160, true);
    check_seam_at_size(1920, 1081, true);
    check_seam_at_size(1919, 1080, true);
}

/* The odd-size frames also give each eye only its own colour straight ahead. */
static void flat_odd_sizes_keep_eyes_apart(void **state)
{
    (void)state;
    const xrd_filter filters[] = {XRD_FILTER_BILINEAR, XRD_FILTER_BICUBIC};
    const struct { int w, h; bool tb; } cases[] = {{1919, 1080, false}, {1920, 1081, true}};
    for (size_t c = 0; c < 2; c++) {
        int n = cases[c].tb ? cases[c].h : cases[c].w;
        /* middle texel blue: it must not reach the left (red) viewport */
        GLuint tex = make_split_tex(cases[c].w, cases[c].h, cases[c].tb, n / 2);
        xrd_layout layout = cases[c].tb ? XRD_LAYOUT_TB_LR : XRD_LAYOUT_SBS_LR;
        double f = (double)cases[c].w / cases[c].h;
        xrd_view_geom g;
        flat_geom(XRD_ZOOM_SCREEN, 1.f,
                  xrd_picture_aspect(layout, xrd_packing_resolve(layout, XRD_PACKING_AUTO, f), f),
                  &g);
        for (size_t fi = 0; fi < 2; fi++) {
            xrd_renderer_set_filter(g_r, filters[fi]);
            draw_flat(tex, XRD_QUAT_IDENTITY, layout, false, &g);
            int max_blue, n_red, n_black;
            viewport_stats(0, 2, 0, &max_blue, &n_red, &n_black);
            if (max_blue > 8)
                fail_msg("%dx%d: blue up to %d/255 in the left viewport", cases[c].w,
                         cases[c].h, max_blue);
            assert_true(n_red > 0);
        }
        glDeleteTextures(1, &tex);
    }
}

/* ---- trilinear sampling (mipmaps) ---- */

/* Generates levels 1..XRD_MIP_MAX_LEVEL of tex and samples it trilinearly, as
 * the player does while shrinking. */
static void mipmap_tex(GLuint tex)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, XRD_MIP_MAX_LEVEL);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

/* A device-sized target, two 1920 x 1080 eyes as on the Viture Pro XR, so a
 * zoom shrinks a frame as much as it would on the glasses (flat zoom 0.25
 * shrinks a 3840x2160 half-SBS frame 8x vertically). Same eye aspect as W x H. */
#define BIG_W 3840
#define BIG_H 1080
static GLuint g_big_fbo, g_big_color;
static uint8_t *g_big;

static void big_begin(void)
{
    g_big = malloc((size_t)BIG_W * BIG_H * 4);
    assert_non_null(g_big);
    glGenTextures(1, &g_big_color);
    glBindTexture(GL_TEXTURE_2D, g_big_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BIG_W, BIG_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &g_big_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_big_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_big_color, 0);
    assert_int_equal(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
}

static void big_end(void)
{
    glDeleteFramebuffers(1, &g_big_fbo);
    glDeleteTextures(1, &g_big_color);
    free(g_big);
    g_big = NULL;
}

/* draw_dp into the device-sized target. */
static void draw_big(GLuint tex, const xrd_view_geom *g, xrd_layout layout, bool trilinear)
{
    float rot[9];
    xrd_quat_to_mat3(XRD_QUAT_IDENTITY, rot);
    const xrd_draw_params dp = {.proj = XRD_PROJ_FLAT, .layout = layout, .swap_eyes = false,
                                .geom = *g, .trilinear = trilinear};
    glBindFramebuffer(GL_FRAMEBUFFER, g_big_fbo);
    glViewport(0, 0, BIG_W, BIG_H);
    glClearColor(1.f, 0.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    xrd_renderer_draw(g_r, tex, rot, &dp, BIG_W, BIG_H);
    assert_int_equal(glGetError(), GL_NO_ERROR);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, BIG_W, BIG_H, GL_RGBA, GL_UNSIGNED_BYTE, g_big);
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

/* viewport_stats for the device-sized target. */
static void big_stats(int eye, int c, int lit, int *max_c, int *n_lit)
{
    *max_c = 0;
    *n_lit = 0;
    for (int y = 0; y < BIG_H; y++)
        for (int x = eye * (BIG_W / 2); x < (eye + 1) * (BIG_W / 2); x++) {
            const uint8_t *p = &g_big[((size_t)y * BIG_W + x) * 4];
            if (p[c] > *max_c)
                *max_c = p[c];
            *n_lit += p[lit] > 200;
        }
}

/* Review Focus 4 at every mipmap level. Each frame's first eye is red and
 * its second blue, the middle texel of an odd split axis coloured as either
 * eye in turn; at flat zooms 0.25 to 0.5 the whole picture, both seam-side
 * edges included, is shrunk 2x to 8x into a 1920 x 1080 viewport. With
 * trilinear (and, for comparison, bilinear and Catmull-Rom on level 0) the
 * left viewport shows no blue and the right one no red above 8/255. */
static void trilinear_keeps_eyes_apart_when_shrinking(void **state)
{
    (void)state;
    const struct { int w, h; xrd_layout layout; } frames[] = {
        {3840, 2160, XRD_LAYOUT_SBS_LR}, /* half SBS */
        {3840, 2160, XRD_LAYOUT_TB_LR},  /* half top-bottom */
        {1920, 2160, XRD_LAYOUT_TB_LR},  /* full top-bottom */
        {1919, 1080, XRD_LAYOUT_SBS_LR}, /* odd split axes */
        {1920, 1081, XRD_LAYOUT_TB_LR},
    };
    const float zooms[] = {0.25f, 0.35f, 0.5f};
    const struct { bool tri; xrd_filter f; const char *name; } modes[] = {
        {true, XRD_FILTER_BICUBIC, "trilinear"},
        {false, XRD_FILTER_BILINEAR, "bilinear"},
        {false, XRD_FILTER_BICUBIC, "Catmull-Rom"},
    };
    big_begin();
    for (size_t fr = 0; fr < sizeof frames / sizeof frames[0]; fr++) {
        int w = frames[fr].w, h = frames[fr].h;
        bool tb = xrd_layout_is_tb(frames[fr].layout);
        int n = tb ? h : w;
        double f = (double)w / h;
        double a = xrd_picture_aspect(
            frames[fr].layout, xrd_packing_resolve(frames[fr].layout, XRD_PACKING_AUTO, f), f);
        for (int which = 0; which < (n % 2 ? 2 : 1); which++) {
            /* which 0: the middle texel blue; which 1: red */
            GLuint tex = make_split_tex(w, h, tb, which == 0 ? n / 2 : (n + 1) / 2);
            mipmap_tex(tex);
            for (size_t z = 0; z < sizeof zooms / sizeof zooms[0]; z++) {
                xrd_view_geom g;
                flat_geom(XRD_ZOOM_SCREEN, zooms[z], a, &g);
                for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
                    xrd_renderer_set_filter(g_r, modes[m].f);
                    draw_big(tex, &g, frames[fr].layout, modes[m].tri);
                    for (int eye = 0; eye < 2; eye++) {
                        int max_other, n_own;
                        big_stats(eye, eye == 0 ? 2 : 0, eye == 0 ? 0 : 2, &max_other, &n_own);
                        if (max_other > 8)
                            fail_msg("%dx%d %s, middle texel %s, zoom %.2f, %s: %s viewport "
                                     "has the other eye's colour up to %d/255",
                                     w, h, tb ? "tb-lr" : "sbs-lr", which ? "red" : "blue",
                                     (double)zooms[z], modes[m].name, eye ? "right" : "left",
                                     max_other);
                        if (n_own < 1000)
                            fail_msg("%dx%d zoom %.2f %s: only %d own-colour pixels in the %s "
                                     "viewport", w, h, (double)zooms[z], modes[m].name, n_own,
                                     eye ? "right" : "left");
                    }
                }
            }
            glDeleteTextures(1, &tex);
        }
    }
    big_end();
}

/* One axis of the widest generation filter the margin allows: dst texel j
 * of n_d (footprint [j r, (j + 1) r) in source texels, r = n_s / n_d) is the
 * plain mean of every source texel whose centre lies strictly within that
 * footprint widened by one source texel each side. first[j] and count[j] list
 * those texels. */
static void wide_taps(int n_s, int n_d, int *first, int *count)
{
    double r = (double)n_s / n_d;
    for (int j = 0; j < n_d; j++) {
        double lo = j * r - 1.0, hi = (j + 1) * r + 1.0;
        int a = (int)ceil(lo - 0.5), b = (int)floor(hi - 0.5);
        if (a + 0.5 <= lo)
            a++;
        if (b + 0.5 >= hi)
            b--;
        if (a < 0)
            a = 0;
        if (b > n_s - 1)
            b = n_s - 1;
        first[j] = a;
        count[j] = b - a + 1;
    }
}

/* Uploads levels 1..XRD_MIP_MAX_LEVEL of an RGBA8 w x h frame (rgba, level 0
 * already in tex) computed on the CPU with the wide filter above, as a driver
 * filtering more widely than llvmpipe might. */
static void wide_mipmap_tex(GLuint tex, const uint8_t *rgba, int w, int h)
{
    float *src = malloc((size_t)w * h * 4 * sizeof *src);
    assert_non_null(src);
    for (size_t i = 0; i < (size_t)w * h * 4; i++)
        src[i] = rgba[i];
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int level = 1; level <= XRD_MIP_MAX_LEVEL; level++) {
        int dw = w / 2 > 1 ? w / 2 : 1, dh = h / 2 > 1 ? h / 2 : 1;
        int *fx = malloc(sizeof(int) * (size_t)dw), *cx = malloc(sizeof(int) * (size_t)dw);
        int *fy = malloc(sizeof(int) * (size_t)dh), *cy = malloc(sizeof(int) * (size_t)dh);
        float *dst = malloc((size_t)dw * dh * 4 * sizeof *dst);
        uint8_t *up = malloc((size_t)dw * dh * 4);
        assert_true(fx && cx && fy && cy && dst && up);
        wide_taps(w, dw, fx, cx);
        wide_taps(h, dh, fy, cy);
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++)
                for (int c = 0; c < 4; c++) {
                    double sum = 0.0;
                    for (int sy = fy[y]; sy < fy[y] + cy[y]; sy++)
                        for (int sx = fx[x]; sx < fx[x] + cx[x]; sx++)
                            sum += src[((size_t)sy * w + sx) * 4 + c];
                    float v = (float)(sum / (cx[x] * cy[y]));
                    dst[((size_t)y * dw + x) * 4 + c] = v;
                    up[((size_t)y * dw + x) * 4 + c] = (uint8_t)lrintf(v);
                }
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, up);
        free(fx);
        free(cx);
        free(fy);
        free(cy);
        free(up);
        free(src);
        src = dst;
        w = dw;
        h = dh;
    }
    free(src);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, XRD_MIP_MAX_LEVEL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    assert_int_equal(glGetError(), GL_NO_ERROR);
}

/* Review Focus 4 does not rest on llvmpipe's box filter: with a chain made
 * by the widest filter the margin allows, trilinear still keeps the eyes
 * apart at 2x to 8x shrink. */
static void trilinear_margin_covers_a_wide_mipmap_filter(void **state)
{
    (void)state;
    const struct { int w, h; xrd_layout layout; } frames[] = {
        {3840, 2160, XRD_LAYOUT_SBS_LR},
        {1919, 1080, XRD_LAYOUT_SBS_LR},
        {1920, 1081, XRD_LAYOUT_TB_LR},
    };
    const float zooms[] = {0.25f, 0.35f, 0.5f};
    big_begin();
    for (size_t fr = 0; fr < sizeof frames / sizeof frames[0]; fr++) {
        int w = frames[fr].w, h = frames[fr].h;
        bool tb = xrd_layout_is_tb(frames[fr].layout);
        int n = tb ? h : w;
        double f = (double)w / h;
        double a = xrd_picture_aspect(
            frames[fr].layout, xrd_packing_resolve(frames[fr].layout, XRD_PACKING_AUTO, f), f);
        for (int which = 0; which < (n % 2 ? 2 : 1); which++) {
            int split = which == 0 ? n / 2 : (n + 1) / 2;
            uint8_t *px = malloc((size_t)w * h * 4);
            assert_non_null(px);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    bool red = (tb ? h - 1 - y : x) < split;
                    uint8_t *p = &px[((size_t)y * w + x) * 4];
                    p[0] = red ? 255 : 0;
                    p[1] = 0;
                    p[2] = red ? 0 : 255;
                    p[3] = 255;
                }
            GLuint tex = make_tex(w, h, px);
            wide_mipmap_tex(tex, px, w, h);
            free(px);
            for (size_t z = 0; z < sizeof zooms / sizeof zooms[0]; z++) {
                xrd_view_geom g;
                flat_geom(XRD_ZOOM_SCREEN, zooms[z], a, &g);
                draw_big(tex, &g, frames[fr].layout, true);
                for (int eye = 0; eye < 2; eye++) {
                    int max_other, n_own;
                    big_stats(eye, eye == 0 ? 2 : 0, eye == 0 ? 0 : 2, &max_other, &n_own);
                    if (max_other > 0)
                        fail_msg("%dx%d %s, middle texel %s, zoom %.2f, wide filter: %s "
                                 "viewport has the other eye's colour up to %d/255",
                                 w, h, tb ? "tb-lr" : "sbs-lr", which ? "red" : "blue",
                                 (double)zooms[z], eye ? "right" : "left", max_other);
                    assert_true(n_own >= 1000);
                }
            }
            glDeleteTextures(1, &tex);
        }
    }
    big_end();
}

/* 1-texel black and white checkerboard, 2000 x 1125 (16:9), mipmapped. */
#define CHECK_W 2000
#define CHECK_H 1125

/* Largest distance from mid grey (127.5) over any channel, in a 200 x 120
 * pixel block at the centre of the left viewport. */
static double centre_grey_error(void)
{
    double worst = 0.0;
    for (int y = BIG_H / 2 - 60; y < BIG_H / 2 + 60; y++)
        for (int x = BIG_W / 4 - 100; x < BIG_W / 4 + 100; x++)
            for (int c = 0; c < 3; c++) {
                double e = fabs(g_big[((size_t)y * BIG_W + x) * 4 + c] - 127.5);
                if (e > worst)
                    worst = e;
            }
    return worst;
}

static void trilinear_averages_checkerboard_at_4x_shrink(void **state)
{
    (void)state;
    uint8_t *px = malloc((size_t)CHECK_W * CHECK_H * 4);
    assert_non_null(px);
    for (int y = 0; y < CHECK_H; y++)
        for (int x = 0; x < CHECK_W; x++) {
            uint8_t v = (x + y) % 2 ? 255 : 0;
            uint8_t *p = &px[((size_t)y * CHECK_W + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    GLuint tex = make_tex(CHECK_W, CHECK_H, px);
    free(px);
    mipmap_tex(tex);
    big_begin();
    /* zoom 0.25: the picture is 480 x 270 pixels, 4.17 texels per pixel */
    xrd_view_geom g;
    flat_geom(XRD_ZOOM_SCREEN, 0.25f, (double)CHECK_W / CHECK_H, &g);
    assert_float_equal(xrd_minify_ratio(XRD_PROJ_FLAT, &g, CHECK_W, CHECK_H, BIG_W / 2, BIG_H,
                                        180.f, 180.f),
                       4.17, 0.01);
    draw_big(tex, &g, XRD_LAYOUT_MONO, true);
    double tri = centre_grey_error();
    xrd_renderer_set_filter(g_r, XRD_FILTER_BILINEAR);
    draw_big(tex, &g, XRD_LAYOUT_MONO, false);
    double off = centre_grey_error();
    big_end();
    glDeleteTextures(1, &tex);
    if (tri > 16.0)
        fail_msg("trilinear: up to %.1f/255 from mid grey, want <= 16", tri);
    if (off <= 16.0)
        fail_msg("without trilinear: only %.1f/255 from mid grey; the test does not alias", off);
}

/* Enlarging, trilinear reads level 0 exactly as bilinear does: same pixels
 * at the seam edge of every split, even and odd (no wider margin there). The
 * frame is noise, so a margin of even one more texel changes the picture. */
static void trilinear_enlarging_matches_bilinear(void **state)
{
    (void)state;
    const struct { int w, h; bool tb; } cases[] = {
        {1920, 1080, false}, {1919, 1080, false}, {1920, 1081, true}};
    static uint8_t bil[W * H * 4];
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        int w = cases[c].w, h = cases[c].h;
        bool tb = cases[c].tb;
        xrd_layout layout = tb ? XRD_LAYOUT_TB_LR : XRD_LAYOUT_SBS_LR;
        uint8_t *px = malloc((size_t)w * h * 4);
        assert_non_null(px);
        uint32_t seed = 12345u;
        for (size_t i = 0; i < (size_t)w * h * 4; i++) {
            seed = seed * 1664525u + 1013904223u;
            px[i] = (i % 4 == 3) ? 255 : (uint8_t)(seed >> 24);
        }
        GLuint tex = make_tex(w, h, px);
        free(px);
        mipmap_tex(tex);
        xrd_view_geom g;
        flat_geom(XRD_ZOOM_MAGNIFY, 20.f,
                  xrd_picture_aspect(layout, tb ? XRD_PACKING_FULL : XRD_PACKING_HALF,
                                     (double)w / h),
                  &g);
        double to_edge = atan(tb ? g.half_h : g.half_w) * 180.0 / 3.14159265358979323846;
        xrd_quat q = tb ? xrd_quat_from_ypr_deg(0, -to_edge, 0)
                        : xrd_quat_from_ypr_deg(-to_edge, 0, 0);
        xrd_draw_params dp = {.proj = XRD_PROJ_FLAT, .layout = layout, .geom = g};
        xrd_renderer_set_filter(g_r, XRD_FILTER_BILINEAR);
        draw_dp(tex, q, &dp);
        memcpy(bil, g_img, sizeof bil);
        dp.trilinear = true;
        draw_dp(tex, q, &dp);
        if (memcmp(bil, g_img, sizeof bil) != 0)
            fail_msg("%dx%d %s: trilinear differs from bilinear when enlarging 20x", w, h,
                     tb ? "tb-lr" : "sbs-lr");
        glDeleteTextures(1, &tex);
    }
}

/* Top-bottom is the same code path for VR180. */
static void hequirect_tb_lr(void **state)
{
    (void)state;
    GLuint tex = make_split_tex(64, 72, true, 36);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_TB_LR, false, false);
    expect_viewport_colour(0, 255, 0, 0);
    expect_viewport_colour(1, 0, 0, 255);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_TB_RL, false, false);
    expect_viewport_colour(0, 0, 0, 255);
    expect_viewport_colour(1, 255, 0, 0);
    draw_tex(tex, XRD_QUAT_IDENTITY, XRD_LAYOUT_TB_LR, true, false);
    expect_viewport_colour(0, 0, 0, 255);
    expect_viewport_colour(1, 255, 0, 0);
    glDeleteTextures(1, &tex);
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
        cmocka_unit_test(draw_ignores_caller_state),
        cmocka_unit_test(yaw_left_samples_left),
        cmocka_unit_test(yaw_right_samples_right),
        cmocka_unit_test(pitch_up_samples_top),
        cmocka_unit_test(pitch_down_samples_bottom),
        cmocka_unit_test(fov90_identity_still_red_blue),
        cmocka_unit_test(fov90_outside_yaw_is_black),
        cmocka_unit_test(fov90_inside_yaw_is_not_black),
        cmocka_unit_test(fov90_pitch_outside_is_black),
        cmocka_unit_test(fov90_edge_splits_the_view),
        cmocka_unit_test(fov180x90_rejects_only_vertically),
        cmocka_unit_test_teardown(convergence_zero_centres_line, reset_view),
        cmocka_unit_test_teardown(convergence_positive_separates_left_left, reset_view),
        cmocka_unit_test_teardown(convergence_negative_reverses, reset_view),
        cmocka_unit_test_teardown(convergence_applies_in_mono_and_swap, reset_view),
        cmocka_unit_test_teardown(convergence_pitched_shifts_horizontally, reset_view),
        cmocka_unit_test_teardown(bicubic_matches_catmull_rom_reference, reset_view),
        cmocka_unit_test_teardown(bicubic_vertical_matches_catmull_rom_reference, reset_view),
        cmocka_unit_test_teardown(bicubic_differs_from_bilinear_near_edge, reset_view),
        cmocka_unit_test_teardown(bicubic_keeps_flat_colour, reset_view),
        cmocka_unit_test_teardown(bicubic_no_seam_bleed, reset_view),
        cmocka_unit_test_teardown(bicubic_identity_left_is_red, reset_view),
        cmocka_unit_test_teardown(tenbit_precision_survives_dither, reset_view),
        cmocka_unit_test_teardown(dither_is_bounded, reset_view),
        cmocka_unit_test_teardown(dither_keeps_black_black, reset_view),
        cmocka_unit_test_teardown(flat_mono_centre_edge_and_outside, reset_view),
        cmocka_unit_test_teardown(flat_behind_is_black, reset_view),
        cmocka_unit_test_teardown(flat_half_sbs_eyes, reset_view),
        cmocka_unit_test_teardown(flat_full_tb_eyes, reset_view),
        cmocka_unit_test_teardown(flat_seam_even_and_odd_sizes, reset_view),
        cmocka_unit_test_teardown(flat_odd_sizes_keep_eyes_apart, reset_view),
        cmocka_unit_test_teardown(hequirect_tb_lr, reset_view),
        cmocka_unit_test_teardown(trilinear_keeps_eyes_apart_when_shrinking, reset_view),
        cmocka_unit_test_teardown(trilinear_margin_covers_a_wide_mipmap_filter, reset_view),
        cmocka_unit_test_teardown(trilinear_averages_checkerboard_at_4x_shrink, reset_view),
        cmocka_unit_test_teardown(trilinear_enlarging_matches_bilinear, reset_view),
        cmocka_unit_test(no_gl_error),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}
