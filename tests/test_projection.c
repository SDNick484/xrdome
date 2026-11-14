#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <cmocka.h>

#include "math/quat.h"
#include "render/projection.h"

#define TOL 1e-3

static void fov_viture_pro(void **state)
{
    (void)state;
    float th, tv;
    xrd_fov_tan_half(43.f, 16.f / 9.f, 1.f, &th, &tv);
    assert_float_equal(th, 0.3433, TOL);
    assert_float_equal(tv, 0.1931, TOL);
}

static void fov_xreal(void **state)
{
    (void)state;
    float th, tv;
    xrd_fov_tan_half(45.f, 16.f / 9.f, 1.f, &th, &tv);
    assert_float_equal(th, 0.3610, TOL);
    assert_float_equal(tv, 0.2031, TOL);
}

static void fov_zoom_2(void **state)
{
    (void)state;
    float th, tv;
    xrd_fov_tan_half(43.f, 16.f / 9.f, 2.f, &th, &tv);
    assert_float_equal(th, 0.1717, TOL);
    assert_float_equal(tv, 0.0966, TOL);
}

static void uv_forward(void **state)
{
    (void)state;
    const float d[3] = {0.f, 0.f, -1.f};
    float uv[2];
    assert_true(xrd_dir_to_uv(d, uv));
    assert_float_equal(uv[0], 0.5f, TOL);
    assert_float_equal(uv[1], 0.5f, TOL);
}

static void uv_right_edge(void **state)
{
    (void)state;
    const float d[3] = {1.f, 0.f, 0.f};
    float uv[2];
    assert_true(xrd_dir_to_uv(d, uv));
    assert_float_equal(uv[0], 1.0f, 0.f);
    assert_float_equal(uv[1], 0.5f, TOL);
}

static void uv_left_edge(void **state)
{
    (void)state;
    const float d[3] = {-1.f, 0.f, 0.f};
    float uv[2];
    assert_true(xrd_dir_to_uv(d, uv));
    assert_float_equal(uv[0], 0.0f, 0.f);
}

static void uv_up(void **state)
{
    (void)state;
    const float d[3] = {0.f, 1.f, 0.f};
    float uv[2];
    assert_true(xrd_dir_to_uv(d, uv));
    assert_float_equal(uv[0], 0.5f, TOL);
    assert_float_equal(uv[1], 1.0f, 0.f);
}

static void uv_down(void **state)
{
    (void)state;
    const float d[3] = {0.f, -1.f, 0.f};
    float uv[2];
    assert_true(xrd_dir_to_uv(d, uv));
    assert_float_equal(uv[0], 0.5f, TOL);
    assert_float_equal(uv[1], 0.0f, 0.f);
}

static void uv_behind(void **state)
{
    (void)state;
    const float d[3] = {0.f, 0.f, 1.f};
    float uv[2];
    assert_false(xrd_dir_to_uv(d, uv));
}

static void uv_just_behind(void **state)
{
    (void)state;
    const float d[3] = {1.f, 0.f, 0.001f};
    float uv[2];
    assert_false(xrd_dir_to_uv(d, uv));
}

static void uv_unnormalised_input(void **state)
{
    (void)state;
    const float d[3] = {0.f, 0.f, -5.f};
    float uv[2];
    assert_true(xrd_dir_to_uv(d, uv));
    assert_float_equal(uv[0], 0.5f, TOL);
    assert_float_equal(uv[1], 0.5f, TOL);
}

static void uv_degenerate_rejected(void **state)
{
    (void)state;
    const float zero[3] = {0.f, 0.f, 0.f};
    const float nan_d[3] = {NAN, 0.f, -1.f};
    const float inf_d[3] = {0.f, INFINITY, -1.f};
    float uv[2];
    assert_false(xrd_dir_to_uv(zero, uv));
    assert_false(xrd_dir_to_uv(nan_d, uv));
    assert_false(xrd_dir_to_uv(inf_d, uv));
}

static void uv_clamped_in_range(void **state)
{
    (void)state;
    const float dirs[][3] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                             {0.f, -1.f, 0.f}, {1.f, 1.f, -1e-9f}, {-3.f, 2.f, 0.f}};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        float uv[2];
        assert_true(xrd_dir_to_uv(dirs[i], uv));
        assert_true(uv[0] >= 0.f && uv[0] <= 1.f);
        assert_true(uv[1] >= 0.f && uv[1] <= 1.f);
    }
}

static const float DEG = 3.14159265358979f / 180.f;

/* Direction at longitude lon, latitude lat (degrees), GL axes. */
static void dir_at(float lon_deg, float lat_deg, float d[3])
{
    float lon = lon_deg * DEG, lat = lat_deg * DEG;
    d[0] = cosf(lat) * sinf(lon);
    d[1] = sinf(lat);
    d[2] = -cosf(lat) * cosf(lon);
}

static void fov_forward_is_centre(void **state)
{
    (void)state;
    const float d[3] = {0.f, 0.f, -1.f};
    float uv[2];
    assert_true(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
    assert_float_equal(uv[0], 0.5f, TOL);
    assert_float_equal(uv[1], 0.5f, TOL);
}

static void fov_horizontal_edge(void **state)
{
    (void)state;
    float d[3], uv[2];
    dir_at(60.f, 0.f, d);
    assert_true(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
    assert_float_equal(uv[0], 1.0f, TOL);
    assert_float_equal(uv[1], 0.5f, TOL);
    dir_at(-60.f, 0.f, d);
    assert_true(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
    assert_float_equal(uv[0], 0.0f, TOL);
    dir_at(61.f, 0.f, d);
    assert_false(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
    dir_at(-61.f, 0.f, d);
    assert_false(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
}

static void fov_vertical_edge(void **state)
{
    (void)state;
    float d[3], uv[2];
    dir_at(0.f, 45.f, d);
    assert_true(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
    assert_float_equal(uv[0], 0.5f, TOL);
    assert_float_equal(uv[1], 1.0f, TOL);
    dir_at(0.f, -45.f, d);
    assert_true(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
    assert_float_equal(uv[1], 0.0f, TOL);
    dir_at(0.f, 46.f, d);
    assert_false(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
    dir_at(0.f, -46.f, d);
    assert_false(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
}

static void fov_partial_scales_uv(void **state)
{
    (void)state;
    float d[3], uv[2];
    dir_at(30.f, 15.f, d);
    assert_true(xrd_dir_to_uv_fov(d, 120.f, 90.f, uv));
    assert_float_equal(uv[0], 30.f / 120.f + 0.5f, TOL);
    assert_float_equal(uv[1], 15.f / 90.f + 0.5f, TOL);
}

static void fov_behind_and_degenerate(void **state)
{
    (void)state;
    const float behind[3] = {0.f, 0.f, 1.f};
    const float zero[3] = {0.f, 0.f, 0.f};
    float uv[2];
    assert_false(xrd_dir_to_uv_fov(behind, 120.f, 90.f, uv));
    assert_false(xrd_dir_to_uv_fov(behind, 180.f, 180.f, uv));
    assert_false(xrd_dir_to_uv_fov(zero, 120.f, 90.f, uv));
}

static void fov_180_agrees_with_plain(void **state)
{
    (void)state;
    const float dirs[][3] = {{0.f, 0.f, -1.f}, {1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f},
                             {0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}, {0.3f, 0.2f, -1.f},
                             {-2.f, 1.f, -0.5f}, {1.f, 1.f, -1e-9f}, {0.f, 0.f, 1.f},
                             {1.f, 0.f, 0.001f}};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        float a[2] = {-1.f, -1.f}, b[2] = {-1.f, -1.f};
        bool ra = xrd_dir_to_uv(dirs[i], a);
        bool rb = xrd_dir_to_uv_fov(dirs[i], 180.f, 180.f, b);
        assert_true(ra == rb);
        if (ra) {
            assert_float_equal(a[0], b[0], 1e-6);
            assert_float_equal(a[1], b[1], 1e-6);
        }
    }
}

static void fov_180_still_clamps_not_rejects(void **state)
{
    (void)state;
    const float d[3] = {1.f, 0.f, 0.f};
    float uv[2];
    assert_true(xrd_dir_to_uv_fov(d, 180.f, 180.f, uv));
    assert_float_equal(uv[0], 1.0f, 0.f);
    const float up[3] = {0.f, 1.f, 0.f};
    assert_true(xrd_dir_to_uv_fov(up, 180.f, 180.f, uv));
    assert_float_equal(uv[1], 1.0f, 0.f);
}

static void fov_partial_never_returns_out_of_range(void **state)
{
    (void)state;
    for (int lon = -90; lon <= 90; lon += 5)
        for (int lat = -90; lat <= 90; lat += 5) {
            float d[3], uv[2];
            dir_at((float)lon, (float)lat, d);
            if (xrd_dir_to_uv_fov(d, 100.f, 60.f, uv)) {
                assert_true(uv[0] >= 0.f && uv[0] <= 1.f);
                assert_true(uv[1] >= 0.f && uv[1] <= 1.f);
                assert_true(abs(lon) <= 50 && abs(lat) <= 30);
            } else {
                assert_true(abs(lon) > 50 || abs(lat) > 30);
            }
        }
}

/* On an axis covering 180 degrees nothing is rejected, only clamped. A front
 * direction (z <= 0) has |lon| <= 90 degrees by construction, so "nudged past
 * 90 by 1e-4 rad" cannot be built from a valid input; the closest we can do is
 * z just below zero, which must stay inside. */
static void fov_180_axis_never_rejects(void **state)
{
    (void)state;
    float uv[2];
    const float edge[3] = {1.f, 0.f, 0.f};
    const float near_edge[3] = {1.f, 0.f, -1e-9f};
    assert_true(xrd_dir_to_uv_fov(edge, 180.f, 90.f, uv));
    assert_float_equal(uv[0], 1.0f, 0.f);
    assert_true(xrd_dir_to_uv_fov(near_edge, 180.f, 90.f, uv));
    assert_float_equal(uv[0], 1.0f, 1e-6);
    assert_true(xrd_dir_to_uv_fov(near_edge, 180.f, 180.f, uv));
    float d[3];
    dir_at(0.f, 46.f, d);
    assert_false(xrd_dir_to_uv_fov(d, 180.f, 90.f, uv));
    dir_at(0.f, 45.f, d);
    assert_true(xrd_dir_to_uv_fov(d, 180.f, 90.f, uv));
    /* and the mirror: 90 x 180 rejects in longitude only */
    dir_at(46.f, 0.f, d);
    assert_false(xrd_dir_to_uv_fov(d, 90.f, 180.f, uv));
    const float up[3] = {0.f, 1.f, 0.f};
    assert_true(xrd_dir_to_uv_fov(up, 90.f, 180.f, uv));
    assert_float_equal(uv[1], 1.0f, 0.f);
}

/* ---- eye sub-image transform, both axes ---- */

static void check_eye(xrd_layout l, int eye, bool swap, float sx, float ox, float sy, float oy)
{
    float s[2] = {-1.f, -1.f}, o[2] = {-1.f, -1.f};
    xrd_eye_uv_transform(l, eye, swap, s, o);
    if (s[0] != sx || o[0] != ox || s[1] != sy || o[1] != oy)
        fail_msg("layout %d eye %d swap %d: scale (%g, %g) offset (%g, %g), want (%g, %g) (%g, %g)",
                 (int)l, eye, (int)swap, (double)s[0], (double)s[1], (double)o[0], (double)o[1],
                 (double)sx, (double)sy, (double)ox, (double)oy);
}

static void eye_sbs_lr(void **state)
{
    (void)state;
    check_eye(XRD_LAYOUT_SBS_LR, 0, false, 0.5f, 0.0f, 1.0f, 0.0f);
    check_eye(XRD_LAYOUT_SBS_LR, 1, false, 0.5f, 0.5f, 1.0f, 0.0f);
    check_eye(XRD_LAYOUT_SBS_LR, 0, true, 0.5f, 0.5f, 1.0f, 0.0f);
    check_eye(XRD_LAYOUT_SBS_LR, 1, true, 0.5f, 0.0f, 1.0f, 0.0f);
}

static void eye_sbs_rl(void **state)
{
    (void)state;
    check_eye(XRD_LAYOUT_SBS_RL, 0, false, 0.5f, 0.5f, 1.0f, 0.0f);
    check_eye(XRD_LAYOUT_SBS_RL, 1, false, 0.5f, 0.0f, 1.0f, 0.0f);
    check_eye(XRD_LAYOUT_SBS_RL, 0, true, 0.5f, 0.0f, 1.0f, 0.0f);
    check_eye(XRD_LAYOUT_SBS_RL, 1, true, 0.5f, 0.5f, 1.0f, 0.0f);
}

static void eye_mono(void **state)
{
    (void)state;
    for (int eye = 0; eye < 2; eye++) {
        check_eye(XRD_LAYOUT_MONO, eye, false, 1.0f, 0.0f, 1.0f, 0.0f);
        check_eye(XRD_LAYOUT_MONO, eye, true, 1.0f, 0.0f, 1.0f, 0.0f);
    }
}

/* tb-lr: the left eye is the top half, and t = 1 is the image top. */
static void eye_tb_lr(void **state)
{
    (void)state;
    check_eye(XRD_LAYOUT_TB_LR, 0, false, 1.0f, 0.0f, 0.5f, 0.5f);
    check_eye(XRD_LAYOUT_TB_LR, 1, false, 1.0f, 0.0f, 0.5f, 0.0f);
    check_eye(XRD_LAYOUT_TB_LR, 0, true, 1.0f, 0.0f, 0.5f, 0.0f);
    check_eye(XRD_LAYOUT_TB_LR, 1, true, 1.0f, 0.0f, 0.5f, 0.5f);
}

static void eye_tb_rl(void **state)
{
    (void)state;
    check_eye(XRD_LAYOUT_TB_RL, 0, false, 1.0f, 0.0f, 0.5f, 0.0f);
    check_eye(XRD_LAYOUT_TB_RL, 1, false, 1.0f, 0.0f, 0.5f, 0.5f);
    check_eye(XRD_LAYOUT_TB_RL, 0, true, 1.0f, 0.0f, 0.5f, 0.5f);
    check_eye(XRD_LAYOUT_TB_RL, 1, true, 1.0f, 0.0f, 0.5f, 0.0f);
}

static void layout_axis_predicates(void **state)
{
    (void)state;
    assert_true(xrd_layout_is_sbs(XRD_LAYOUT_SBS_LR));
    assert_true(xrd_layout_is_sbs(XRD_LAYOUT_SBS_RL));
    assert_false(xrd_layout_is_sbs(XRD_LAYOUT_MONO));
    assert_false(xrd_layout_is_sbs(XRD_LAYOUT_TB_LR));
    assert_false(xrd_layout_is_sbs(XRD_LAYOUT_TB_RL));
    assert_true(xrd_layout_is_tb(XRD_LAYOUT_TB_LR));
    assert_true(xrd_layout_is_tb(XRD_LAYOUT_TB_RL));
    assert_false(xrd_layout_is_tb(XRD_LAYOUT_MONO));
    assert_false(xrd_layout_is_tb(XRD_LAYOUT_SBS_LR));
    assert_false(xrd_layout_is_tb(XRD_LAYOUT_SBS_RL));
}

/* ---- packing and picture aspect (spec 5.3) ---- */

static void packing_auto_rows(void **state)
{
    (void)state;
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_SBS_LR, XRD_PACKING_AUTO, 1920.0 / 1080.0),
                     XRD_PACKING_HALF);
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_SBS_LR, XRD_PACKING_AUTO, 3840.0 / 1080.0),
                     XRD_PACKING_FULL);
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_SBS_RL, XRD_PACKING_AUTO, 3840.0 / 1080.0),
                     XRD_PACKING_FULL);
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_TB_LR, XRD_PACKING_AUTO, 1920.0 / 1080.0),
                     XRD_PACKING_HALF);
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_TB_LR, XRD_PACKING_AUTO, 1920.0 / 2160.0),
                     XRD_PACKING_FULL);
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_TB_RL, XRD_PACKING_AUTO, 1920.0 / 2160.0),
                     XRD_PACKING_FULL);
    /* the thresholds themselves: sbs full at F >= 3.0, tb full below 1.5 */
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_SBS_LR, XRD_PACKING_AUTO, 3.0),
                     XRD_PACKING_FULL);
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_SBS_LR, XRD_PACKING_AUTO, 2.999),
                     XRD_PACKING_HALF);
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_TB_LR, XRD_PACKING_AUTO, 1.5),
                     XRD_PACKING_HALF);
    assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_TB_LR, XRD_PACKING_AUTO, 1.499),
                     XRD_PACKING_FULL);
}

static void packing_explicit_and_mono(void **state)
{
    (void)state;
    const xrd_layout stereo[] = {XRD_LAYOUT_SBS_LR, XRD_LAYOUT_SBS_RL, XRD_LAYOUT_TB_LR,
                                 XRD_LAYOUT_TB_RL};
    const double shapes[] = {16.0 / 9.0, 32.0 / 9.0, 8.0 / 9.0, 0.0, NAN};
    for (size_t i = 0; i < 4; i++)
        for (size_t f = 0; f < 5; f++) {
            assert_int_equal(xrd_packing_resolve(stereo[i], XRD_PACKING_HALF, shapes[f]),
                             XRD_PACKING_HALF);
            assert_int_equal(xrd_packing_resolve(stereo[i], XRD_PACKING_FULL, shapes[f]),
                             XRD_PACKING_FULL);
        }
    const xrd_packing all[] = {XRD_PACKING_AUTO, XRD_PACKING_HALF, XRD_PACKING_FULL};
    for (size_t k = 0; k < 3; k++)
        for (size_t f = 0; f < 5; f++)
            assert_int_equal(xrd_packing_resolve(XRD_LAYOUT_MONO, all[k], shapes[f]),
                             XRD_PACKING_FULL);
}

static void picture_aspect_table(void **state)
{
    (void)state;
    const double f = 1920.0 / 1080.0;
    /* mono: A = F (no half packing exists; it is ignored) */
    assert_float_equal(xrd_picture_aspect(XRD_LAYOUT_MONO, XRD_PACKING_FULL, f), f, 1e-12);
    assert_float_equal(xrd_picture_aspect(XRD_LAYOUT_MONO, XRD_PACKING_HALF, f), f, 1e-12);
    /* sbs: full F / 2, half F */
    assert_float_equal(xrd_picture_aspect(XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL, 2 * f), f, 1e-12);
    assert_float_equal(xrd_picture_aspect(XRD_LAYOUT_SBS_RL, XRD_PACKING_HALF, f), f, 1e-12);
    /* tb: full 2 F, half F */
    assert_float_equal(xrd_picture_aspect(XRD_LAYOUT_TB_LR, XRD_PACKING_FULL, f / 2), f, 1e-12);
    assert_float_equal(xrd_picture_aspect(XRD_LAYOUT_TB_RL, XRD_PACKING_HALF, f), f, 1e-12);
}

/* Review Focus 3: no aspect is NaN, infinite or zero. */
static void picture_aspect_junk_is_16_9(void **state)
{
    (void)state;
    const double junk[] = {0.0, -1.0, NAN, INFINITY, -INFINITY};
    const xrd_layout layouts[] = {XRD_LAYOUT_SBS_LR, XRD_LAYOUT_SBS_RL, XRD_LAYOUT_MONO,
                                  XRD_LAYOUT_TB_LR, XRD_LAYOUT_TB_RL};
    const xrd_packing packs[] = {XRD_PACKING_AUTO, XRD_PACKING_HALF, XRD_PACKING_FULL};
    for (size_t j = 0; j < sizeof junk / sizeof junk[0]; j++)
        for (size_t l = 0; l < 5; l++)
            for (size_t k = 0; k < 3; k++)
                assert_float_equal(xrd_picture_aspect(layouts[l], packs[k], junk[j]),
                                   16.0 / 9.0, 1e-12);
    /* finite F whose doubling overflows is junk too */
    double a = xrd_picture_aspect(XRD_LAYOUT_TB_LR, XRD_PACKING_FULL, 1e308);
    assert_true(isfinite(a) && a > 0.0);
    /* a denormal F that halves to zero */
    a = xrd_picture_aspect(XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL, 4.9e-324);
    assert_true(isfinite(a) && a > 0.0);
}

/* ---- view geometry (spec 5.1, 5.2); Viture Pro XR: 43 degrees, 16:9 eyes ---- */

#define VIT_DIAG 43.f
#define VIT_ASPECT (16.f / 9.f)
#define RAD2DEG (180.0 / 3.14159265358979323846)

static void zoom1_tans(float *th0, float *tv0)
{
    xrd_fov_tan_half(VIT_DIAG, VIT_ASPECT, 1.f, th0, tv0);
}

static void flat_screen_sizes(void **state)
{
    (void)state;
    float th0, tv0;
    zoom1_tans(&th0, &tv0);
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.5f, 16.0 / 9.0, &g);
    assert_float_equal(2 * atan(g.half_w) * RAD2DEG, 54.5, 0.1);
    assert_float_equal(2 * atan(g.half_h) * RAD2DEG, 32.4, 0.1);
    assert_float_equal(g.tan_h, th0, 1e-6);
    assert_float_equal(g.tan_v, tv0, 1e-6);
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 2.0f, 16.0 / 9.0, &g);
    assert_float_equal(2 * atan(g.half_w) * RAD2DEG, 69.0, 0.1);
    assert_float_equal(2 * atan(g.half_h) * RAD2DEG, 42.3, 0.1);
    assert_float_equal(g.tan_h, th0, 1e-6);
}

/* At S = 1 the picture is contained: a wide film touches the sides, a 4:3
 * picture touches the top and bottom. */
static void flat_contain_at_s1(void **state)
{
    (void)state;
    float th0, tv0;
    zoom1_tans(&th0, &tv0);
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.f, 2.39, &g);
    assert_float_equal(g.half_w, th0, 1e-6);
    assert_true(g.half_h < tv0);
    assert_float_equal(g.half_w / g.half_h, 2.39, 1e-5);
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.f, 4.0 / 3.0, &g);
    assert_float_equal(g.half_h, tv0, 1e-6);
    assert_true(g.half_w < th0);
    assert_float_equal(g.half_w / g.half_h, 4.0 / 3.0, 1e-5);
}

static void flat_magnify(void **state)
{
    (void)state;
    float th0, tv0;
    zoom1_tans(&th0, &tv0);
    xrd_view_geom m, s;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_MAGNIFY, VIT_DIAG, VIT_ASPECT, 1.5f, 16.0 / 9.0, &m);
    assert_float_equal(m.half_w, th0, 1e-6); /* S = 1 */
    assert_float_equal(m.tan_h, th0 / 1.5, 1e-6);
    assert_float_equal(m.tan_v, tv0 / 1.5, 1e-6);
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.5f, 16.0 / 9.0, &s);
    /* the same picture straight ahead in both modes */
    assert_float_equal(m.half_w / m.tan_h, s.half_w / s.tan_h, 1e-5);
    assert_float_equal(m.half_h / m.tan_v, s.half_h / s.tan_v, 1e-5);
}

static void hequirect_geometry(void **state)
{
    (void)state;
    const float zooms[] = {0.25f, 1.f, 1.5f, 4.f};
    for (size_t i = 0; i < 4; i++)
        for (int mode = 0; mode < 2; mode++) {
            float th, tv;
            xrd_fov_tan_half(VIT_DIAG, VIT_ASPECT, zooms[i], &th, &tv);
            xrd_view_geom g;
            xrd_view_geometry(XRD_PROJ_HEQUIRECT, (xrd_zoom_mode)mode, VIT_DIAG, VIT_ASPECT,
                              zooms[i], 16.0 / 9.0, &g);
            assert_true(g.tan_h == th);
            assert_true(g.tan_v == tv);
            assert_true(g.half_w == 0.f);
            assert_true(g.half_h == 0.f);
        }
}

/* Review Focus 3: junk aspect or zoom never gives NaN, infinity or zero. */
static void geometry_junk_inputs_stay_finite(void **state)
{
    (void)state;
    const double aspects[] = {0.0, -2.0, NAN, INFINITY};
    const float zooms[] = {0.f, -1.f, NAN, INFINITY, 1.5f};
    for (size_t a = 0; a < 4; a++)
        for (size_t z = 0; z < 5; z++)
            for (int mode = 0; mode < 2; mode++) {
                xrd_view_geom g;
                xrd_view_geometry(XRD_PROJ_FLAT, (xrd_zoom_mode)mode, VIT_DIAG, VIT_ASPECT,
                                  zooms[z], aspects[a], &g);
                assert_true(isfinite(g.tan_h) && g.tan_h > 0.f);
                assert_true(isfinite(g.tan_v) && g.tan_v > 0.f);
                assert_true(isfinite(g.half_w) && g.half_w > 0.f);
                assert_true(isfinite(g.half_h) && g.half_h > 0.f);
                assert_float_equal(g.half_w / g.half_h, 16.0 / 9.0, 1e-5);
            }
}

/* ---- flat mapping ---- */

static void flat_uv_centre_and_edges(void **state)
{
    (void)state;
    const float hw = 0.515f, hh = 0.2897f;
    float uv[2];
    const float fwd[3] = {0.f, 0.f, -1.f};
    assert_true(xrd_flat_dir_to_uv(fwd, hw, hh, uv));
    assert_float_equal(uv[0], 0.5f, 1e-6);
    assert_float_equal(uv[1], 0.5f, 1e-6);

    const float right[3] = {hw, 0.f, -1.f};
    assert_true(xrd_flat_dir_to_uv(right, hw, hh, uv));
    assert_float_equal(uv[0], 1.0f, 1e-6);
    assert_float_equal(uv[1], 0.5f, 1e-6);
    const float left[3] = {-hw, 0.f, -1.f};
    assert_true(xrd_flat_dir_to_uv(left, hw, hh, uv));
    assert_float_equal(uv[0], 0.0f, 1e-6);
    const float top[3] = {0.f, hh, -1.f};
    assert_true(xrd_flat_dir_to_uv(top, hw, hh, uv));
    assert_float_equal(uv[1], 1.0f, 1e-6);
    const float bottom[3] = {0.f, -hh, -1.f};
    assert_true(xrd_flat_dir_to_uv(bottom, hw, hh, uv));
    assert_float_equal(uv[1], 0.0f, 1e-6);

    const float past_right[3] = {1.001f * hw, 0.f, -1.f};
    assert_false(xrd_flat_dir_to_uv(past_right, hw, hh, uv));
    const float past_top[3] = {0.f, 1.001f * hh, -1.f};
    assert_false(xrd_flat_dir_to_uv(past_top, hw, hh, uv));

    /* not normalised: the same direction, longer */
    const float far[3] = {0.5f * hw * 7.f, 0.f, -7.f};
    assert_true(xrd_flat_dir_to_uv(far, hw, hh, uv));
    assert_float_equal(uv[0], 0.75f, 1e-6);
}

static void flat_uv_behind_and_degenerate(void **state)
{
    (void)state;
    float uv[2];
    const float side[3] = {1.f, 0.f, 0.f};
    assert_false(xrd_flat_dir_to_uv(side, 0.5f, 0.3f, uv));
    const float side0[3] = {0.f, 0.f, 0.f};
    assert_false(xrd_flat_dir_to_uv(side0, 0.5f, 0.3f, uv));
    const float minus0[3] = {0.f, 0.f, -0.f};
    assert_false(xrd_flat_dir_to_uv(minus0, 0.5f, 0.3f, uv));
    const float behind[3] = {0.f, 0.f, 1.f};
    assert_false(xrd_flat_dir_to_uv(behind, 0.5f, 0.3f, uv));
    const float tiny_behind[3] = {0.f, 0.f, 1e-30f};
    assert_false(xrd_flat_dir_to_uv(tiny_behind, 0.5f, 0.3f, uv));
    const float nan3[3] = {NAN, 0.f, -1.f};
    assert_false(xrd_flat_dir_to_uv(nan3, 0.5f, 0.3f, uv));
    const float nanz[3] = {0.f, 0.f, NAN};
    assert_false(xrd_flat_dir_to_uv(nanz, 0.5f, 0.3f, uv));
    const float inf3[3] = {INFINITY, 0.f, -1.f};
    assert_false(xrd_flat_dir_to_uv(inf3, 0.5f, 0.3f, uv));
    /* grazing: almost parallel to the plane, far outside any screen */
    const float graze[3] = {1.f, 0.f, -1e-30f};
    assert_false(xrd_flat_dir_to_uv(graze, 0.5f, 0.3f, uv));
    /* a junk screen is never hit */
    const float fwd[3] = {0.f, 0.f, -1.f};
    assert_false(xrd_flat_dir_to_uv(fwd, 0.f, 0.3f, uv));
    assert_false(xrd_flat_dir_to_uv(fwd, 0.5f, NAN, uv));
    assert_false(xrd_flat_dir_to_uv(fwd, INFINITY, 0.3f, uv));
}

/* The screen is flat: its top edge maps to v = 1 all along, whatever the head
 * yaw. The rays are built in head space as the eye pass would (through a
 * yawed head), then rotated to the world, as u_rot does. */
static void flat_top_edge_does_not_bow(void **state)
{
    (void)state;
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.5f, 16.0 / 9.0, &g);
    const double yaws[] = {0.0, 10.0, 25.0};
    const float xs[] = {-0.9f, 0.f, 0.6f};
    for (size_t y = 0; y < 3; y++) {
        xrd_quat head = xrd_quat_from_ypr_deg(yaws[y], 0, 0);
        for (size_t i = 0; i < 3; i++) {
            const float world[3] = {xs[i] * g.half_w, g.half_h, -1.f};
            float local[3], d[3], uv[2];
            xrd_quat_rotate(xrd_quat_conj(head), world, local);
            xrd_quat_rotate(head, local, d);
            assert_true(xrd_flat_dir_to_uv(d, g.half_w, g.half_h, uv));
            if (fabs(uv[1] - 1.0) > 1e-5)
                fail_msg("yaw %.0f x %.2f: v = %.7f, want 1", yaws[y], (double)xs[i],
                         (double)uv[1]);
            assert_float_equal(uv[0], xs[i] * 0.5 + 0.5, 1e-5);
        }
    }
}

/* ---- curved flat screen (--screen-curve) ---- */

/* The direction from the eye to the point at angle phi about the axis of a
 * curve-R cylinder (curve - 1 behind the eye), at height y. */
static void arc_dir(double r, double phi, double y, float d[3])
{
    d[0] = (float)(r * sin(phi));
    d[1] = (float)y;
    d[2] = (float)((r - 1.0) - r * cos(phi));
}

/* off (0) is the plane: the same answer and the same uv bits. */
static void curve_off_is_the_plane(void **state)
{
    (void)state;
    const float vals[] = {0.f, 0.1f, -0.3f, 0.52f, 1.f, -2.f, 1e-30f, NAN, INFINITY};
    const size_t n = sizeof vals / sizeof vals[0];
    const float zs[] = {-1.f, -0.2f, -7.f, 0.f, -0.f, 1.f, 1e-30f, NAN};
    const float screens[][2] = {{0.515f, 0.2897f}, {0.2f, 0.4f}, {0.f, 0.3f}, {NAN, 0.3f}};
    for (size_t s = 0; s < 4; s++)
        for (size_t i = 0; i < n; i++)
            for (size_t j = 0; j < n; j++)
                for (size_t k = 0; k < sizeof zs / sizeof zs[0]; k++) {
                    const float d[3] = {vals[i], vals[j], zs[k]};
                    float a[2] = {-7.f, -7.f}, b[2] = {-7.f, -7.f};
                    bool pa = xrd_flat_dir_to_uv(d, screens[s][0], screens[s][1], a);
                    bool pb = xrd_flat_curve_dir_to_uv(d, screens[s][0], screens[s][1], 0.f, b);
                    assert_true(pa == pb);
                    assert_memory_equal(a, b, sizeof a);
                }
}

static void curve_uv_centre_edges_top_bottom(void **state)
{
    (void)state;
    const float hw = 0.515f, hh = 0.2897f;
    const float curves[] = {1.f, 2.f, 10.f};
    for (size_t c = 0; c < 3; c++) {
        const double r = curves[c], phi_e = hw / r;
        float uv[2], d[3];
        const float fwd[3] = {0.f, 0.f, -1.f};
        assert_true(xrd_flat_curve_dir_to_uv(fwd, hw, hh, curves[c], uv));
        assert_float_equal(uv[0], 0.5f, 1e-6);
        assert_float_equal(uv[1], 0.5f, 1e-6);
        /* the arc's ends, at mid height, then at the top and bottom */
        const double ys[] = {0.0, hh, -hh};
        const float vs[] = {0.5f, 1.f, 0.f};
        for (size_t i = 0; i < 3; i++) {
            arc_dir(r, phi_e, ys[i], d);
            assert_true(xrd_flat_curve_dir_to_uv(d, hw, hh, curves[c], uv));
            assert_float_equal(uv[0], 1.f, 1e-5);
            assert_float_equal(uv[1], vs[i], 1e-5);
            arc_dir(r, -phi_e, ys[i], d);
            assert_true(xrd_flat_curve_dir_to_uv(d, hw, hh, curves[c], uv));
            assert_float_equal(uv[0], 0.f, 1e-5);
            assert_float_equal(uv[1], vs[i], 1e-5);
        }
        /* a quarter of the way along the arc, at the top: the screen keeps its
         * height in every column, so v = 1 there too */
        arc_dir(r, 0.5 * phi_e, hh, d);
        assert_true(xrd_flat_curve_dir_to_uv(d, hw, hh, curves[c], uv));
        assert_float_equal(uv[0], 0.75f, 1e-5);
        assert_float_equal(uv[1], 1.f, 1e-5);
        /* just past the ends and the top */
        arc_dir(r, 1.001 * phi_e, 0.0, d);
        assert_false(xrd_flat_curve_dir_to_uv(d, hw, hh, curves[c], uv));
        arc_dir(r, -1.001 * phi_e, 0.0, d);
        assert_false(xrd_flat_curve_dir_to_uv(d, hw, hh, curves[c], uv));
        arc_dir(r, 0.5 * phi_e, 1.001 * hh, d);
        assert_false(xrd_flat_curve_dir_to_uv(d, hw, hh, curves[c], uv));
        /* not normalised: the same direction, longer */
        arc_dir(r, 0.5 * phi_e, 0.0, d);
        const float far[3] = {7.f * d[0], 7.f * d[1], 7.f * d[2]};
        assert_true(xrd_flat_curve_dir_to_uv(far, hw, hh, curves[c], uv));
        assert_float_equal(uv[0], 0.75f, 1e-5);
    }
    /* radius 1: u is the direction's own yaw, whatever the pitch */
    float uv[2];
    const float d30[3] = {(float)sin(0.3), 0.1f, (float)-cos(0.3)};
    assert_true(xrd_flat_curve_dir_to_uv(d30, 0.5f, 0.3f, 1.f, uv));
    assert_float_equal(uv[0], 0.3 / 1.0 + 0.5, 1e-6);
    assert_float_equal(uv[1], 0.1 / 0.6 + 0.5, 1e-6);
}

static void curve_behind_and_degenerate(void **state)
{
    (void)state;
    float uv[2], d[3];
    /* an arc that reaches past 90 degrees (2 radians each side at radius 1):
     * beside and behind the viewer it is still not drawn */
    arc_dir(1.0, 1.7, 0.0, d);
    assert_true(d[2] > 0.f);
    assert_false(xrd_flat_curve_dir_to_uv(d, 2.f, 0.3f, 1.f, uv));
    const float side[3] = {1.f, 0.f, 0.f};
    assert_false(xrd_flat_curve_dir_to_uv(side, 2.f, 0.3f, 1.f, uv));
    const float behind[3] = {0.f, 0.f, 1.f};
    assert_false(xrd_flat_curve_dir_to_uv(behind, 2.f, 0.3f, 1.f, uv));
    arc_dir(1.0, 1.5, 0.0, d); /* still in front: drawn */
    assert_true(xrd_flat_curve_dir_to_uv(d, 2.f, 0.3f, 1.f, uv));
    assert_float_equal(uv[0], 1.5 / 4.0 + 0.5, 1e-5);
    /* straight up and down, the zero vector, NaN and infinity */
    const float dirs[][3] = {{0.f, 1.f, 0.f}, {0.f, -1.f, -1e-30f}, {0.f, 0.f, 0.f},
                             {0.f, 0.f, -0.f}, {NAN, 0.f, -1.f}, {0.f, 0.f, NAN},
                             {INFINITY, 0.f, -1.f}, {0.f, 0.f, 1e-30f}};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
        assert_false(xrd_flat_curve_dir_to_uv(dirs[i], 0.5f, 0.3f, 1.f, uv));
    /* a junk screen or radius is never hit */
    const float fwd[3] = {0.f, 0.f, -1.f};
    assert_false(xrd_flat_curve_dir_to_uv(fwd, 0.f, 0.3f, 1.f, uv));
    assert_false(xrd_flat_curve_dir_to_uv(fwd, 0.5f, NAN, 1.f, uv));
    assert_false(xrd_flat_curve_dir_to_uv(fwd, INFINITY, 0.3f, 1.f, uv));
    const float curves[] = {0.999f, 0.5f, -1.f, 10.001f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof curves / sizeof curves[0]; i++)
        assert_false(xrd_flat_curve_dir_to_uv(fwd, 0.5f, 0.3f, curves[i], uv));
    assert_true(xrd_flat_curve_dir_to_uv(fwd, 0.5f, 0.3f, XRD_SCREEN_CURVE_MAX, uv));
}

/* Seen through a pinhole, a curved screen's columns stay straight (u is the
 * same all down an image column) and its top edge bows away from the centre
 * (the plane's top edge, v = 1 all along, falls inside the curved screen at
 * the sides), at every head yaw. Rays as in flat_top_edge_does_not_bow. */
static void curve_columns_straight_top_bows(void **state)
{
    (void)state;
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.5f, 16.0 / 9.0, &g);
    /* the screen reaches 29.5 degrees each side; every ray here hits it */
    const double yaws[] = {0.0, 10.0, -12.0};
    for (size_t yi = 0; yi < 3; yi++) {
        xrd_quat head = xrd_quat_from_ypr_deg(yaws[yi], 0, 0);
        const float xs[] = {-0.15f, 0.05f, 0.2f};
        for (size_t i = 0; i < 3; i++) {
            float u0 = -1.f;
            for (int k = -4; k <= 4; k++) {
                const float local[3] = {xs[i], 0.06f * (float)k, -1.f};
                float d[3], uv[2];
                xrd_quat_rotate(head, local, d);
                assert_true(xrd_flat_curve_dir_to_uv(d, g.half_w, g.half_h, 1.f, uv));
                if (k == -4)
                    u0 = uv[0];
                else if (fabs(uv[0] - u0) > 1e-5)
                    fail_msg("yaw %.0f x %.2f: u %.7f at row %d, %.7f at the bottom",
                             yaws[yi], (double)xs[i], (double)uv[0], k, (double)u0);
            }
        }
    }
    /* straight ahead, the plane's top edge: v = 1 only at the centre column.
     * At radius 1 a column at view tangent x is sqrt(1 + x^2) away along the
     * ray, so the plane's top edge is at v = 0.5 + 0.5 / sqrt(1 + x^2). */
    const float xs[] = {0.f, 0.2f, 0.4f};
    for (size_t i = 0; i < 3; i++) {
        const float x = xs[i] * g.half_w;
        const float d[3] = {x, g.half_h, -1.f};
        float uv[2];
        assert_true(xrd_flat_curve_dir_to_uv(d, g.half_w, g.half_h, 1.f, uv));
        assert_float_equal(uv[1], 0.5 + 0.5 / sqrt(1.0 + (double)x * x), 1e-6);
        if (i > 0 && !(uv[1] < 0.999f))
            fail_msg("x %.1f: v = %.5f on the plane's top edge, want below 1", (double)xs[i],
                     (double)uv[1]);
    }
}

static void curve_half_angle(void **state)
{
    (void)state;
    assert_float_equal(xrd_flat_half_angle(0.5f, 0.f), atan(0.5), 1e-9);
    assert_float_equal(xrd_flat_half_angle(0.5f, 1.f), 0.5, 1e-7); /* every column at 1 */
    /* radius 2: the direction of the arc's end, which maps to u = 1 */
    double ang = xrd_flat_half_angle(0.5f, 2.f);
    const float d[3] = {(float)sin(ang), 0.f, (float)-cos(ang)};
    float uv[2];
    assert_true(xrd_flat_curve_dir_to_uv(d, 0.5f, 0.3f, 2.f, uv));
    assert_float_equal(uv[0], 1.f, 1e-5);
    /* wider than the plane at the same width, narrower than radius 1 */
    assert_true(ang > atan(0.5) && ang < 0.5);
    /* an arc past 90 degrees stops there */
    assert_float_equal(xrd_flat_half_angle(2.f, 1.f), 3.14159265358979323846 / 2, 1e-12);
    assert_float_equal(xrd_flat_half_angle(9.f, 1.f), 3.14159265358979323846 / 2, 1e-12);
    /* radius 2: the end comes level with the eye at 60 degrees about the axis */
    assert_float_equal(xrd_flat_half_angle(2.2f, 2.f), 3.14159265358979323846 / 2, 1e-12);
    assert_true(xrd_flat_half_angle(2.0f, 2.f) < 3.14159265358979323846 / 2);
    assert_true(xrd_flat_half_angle(0.f, 1.f) == 0.0);
    assert_true(xrd_flat_half_angle(NAN, 0.f) == 0.0);
    assert_true(xrd_flat_half_angle(0.5f, 0.5f) == 0.0);
    assert_true(xrd_flat_half_angle(0.5f, NAN) == 0.0);
}

/* ---- minification ratio (spec 5.6) ---- */

static void minify_flat_screen(void **state)
{
    (void)state;
    xrd_view_geom g;
    const struct { float zoom; int w, h; double want; } rows[] = {
        {1.5f, 3840, 2160, 1.333}, {1.0f, 3840, 2160, 2.0}, {0.5f, 3840, 2160, 4.0},
        {1.5f, 1920, 1080, 0.667},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, rows[i].zoom,
                          16.0 / 9.0, &g);
        float r = xrd_minify_ratio(XRD_PROJ_FLAT, &g, rows[i].w, rows[i].h, 1920, 1080, 180.f,
                                   180.f);
        if (fabs(r - rows[i].want) > 0.01)
            fail_msg("zoom %.2f %dx%d: ratio %.4f, want %.3f", (double)rows[i].zoom, rows[i].w,
                     rows[i].h, (double)r, rows[i].want);
    }
    /* magnify agrees with screen at the centre */
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_MAGNIFY, VIT_DIAG, VIT_ASPECT, 1.5f, 16.0 / 9.0, &g);
    assert_float_equal(xrd_minify_ratio(XRD_PROJ_FLAT, &g, 3840, 2160, 1920, 1080, 180.f, 180.f),
                       1.333, 0.01);
}

static void minify_flat_takes_larger_axis(void **state)
{
    (void)state;
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.0f, 16.0 / 9.0, &g);
    /* a half-SBS eye squeezed to 1920 x 2160 of a 3840 x 2160 frame: shrunk
     * 2x vertically, 1x horizontally */
    assert_float_equal(xrd_minify_ratio(XRD_PROJ_FLAT, &g, 1920, 2160, 1920, 1080, 180.f, 180.f),
                       2.0, 0.01);
    assert_float_equal(xrd_minify_ratio(XRD_PROJ_FLAT, &g, 3840, 1080, 1920, 1080, 180.f, 180.f),
                       2.0, 0.01);
}

static void minify_hequirect(void **state)
{
    (void)state;
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_HEQUIRECT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.f, 16.0 / 9.0,
                      &g);
    float r = xrd_minify_ratio(XRD_PROJ_HEQUIRECT, &g, 2160, 2160, 1920, 1080, 180.f, 180.f);
    assert_float_equal(r, 0.24, 0.01);
    /* a narrower content FOV packs more pixels per degree */
    float r90 = xrd_minify_ratio(XRD_PROJ_HEQUIRECT, &g, 2160, 2160, 1920, 1080, 90.f, 90.f);
    assert_float_equal(r90, 2.0 * r, 1e-4);
}

static void minify_junk_is_zero(void **state)
{
    (void)state;
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.f, 16.0 / 9.0, &g);
    assert_true(xrd_minify_ratio(XRD_PROJ_FLAT, &g, 0, 2160, 1920, 1080, 180.f, 180.f) == 0.f);
    assert_true(xrd_minify_ratio(XRD_PROJ_FLAT, &g, 3840, 2160, 0, 1080, 180.f, 180.f) == 0.f);
    assert_true(xrd_minify_ratio(XRD_PROJ_FLAT, &g, 3840, 2160, 1920, -1, 180.f, 180.f) == 0.f);
    assert_true(xrd_minify_ratio(XRD_PROJ_FLAT, NULL, 3840, 2160, 1920, 1080, 180.f, 180.f) == 0.f);
    xrd_view_geom bad = g;
    bad.half_w = 0.f;
    assert_true(xrd_minify_ratio(XRD_PROJ_FLAT, &bad, 3840, 2160, 1920, 1080, 180.f, 180.f) == 0.f);
    bad = g;
    bad.tan_v = NAN;
    assert_true(xrd_minify_ratio(XRD_PROJ_FLAT, &bad, 3840, 2160, 1920, 1080, 180.f, 180.f) == 0.f);
    xrd_view_geometry(XRD_PROJ_HEQUIRECT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 1.f, 16.0 / 9.0,
                      &g);
    assert_true(xrd_minify_ratio(XRD_PROJ_HEQUIRECT, &g, 2160, 2160, 1920, 1080, 0.f, 180.f) == 0.f);
    assert_true(xrd_minify_ratio(XRD_PROJ_HEQUIRECT, &g, 2160, 2160, 1920, 1080, 180.f, NAN) == 0.f);
}

/* xrd_minify_ratio serves a curved screen too: at the view centre, straight
 * ahead, u and v change per unit of view tangent at the plane's rates
 * (1 / (2 half_w), 1 / (2 half_h)), measured on xrd_flat_curve_dir_to_uv by
 * central differences; away from the centre they only fall, so the centre
 * is where the picture is shrunk most. */
static void minify_curved_centre_is_the_plane(void **state)
{
    (void)state;
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, VIT_DIAG, VIT_ASPECT, 2.5f, 16.0 / 9.0, &g);
    const double du0 = 1.0 / (2.0 * g.half_w), dv0 = 1.0 / (2.0 * g.half_h);
    const float curves[] = {1.f, 2.f, 10.f};
    const float e = 1e-3f;
    for (size_t c = 0; c < 3; c++) {
        const float xs[] = {0.f, 0.2f, 0.6f};
        for (size_t i = 0; i < 3; i++) {
            float a[2], b[2];
            const float l[3] = {xs[i] - e, 0.f, -1.f}, r[3] = {xs[i] + e, 0.f, -1.f};
            assert_true(xrd_flat_curve_dir_to_uv(l, g.half_w, g.half_h, curves[c], a));
            assert_true(xrd_flat_curve_dir_to_uv(r, g.half_w, g.half_h, curves[c], b));
            double du = (b[0] - a[0]) / (2.0 * e);
            const float dn[3] = {xs[i], -e, -1.f}, up[3] = {xs[i], e, -1.f};
            assert_true(xrd_flat_curve_dir_to_uv(dn, g.half_w, g.half_h, curves[c], a));
            assert_true(xrd_flat_curve_dir_to_uv(up, g.half_w, g.half_h, curves[c], b));
            double dv = (b[1] - a[1]) / (2.0 * e);
            if (i == 0) {
                if (fabs(du / du0 - 1.0) > 1e-3 || fabs(dv / dv0 - 1.0) > 1e-3)
                    fail_msg("curve %.0f centre: du %.5f dv %.5f, plane %.5f %.5f",
                             (double)curves[c], du, dv, du0, dv0);
            } else if (!(du < du0 && dv < dv0 * (1.0 + 1e-3))) {
                fail_msg("curve %.0f x %.1f: du %.5f dv %.5f above the centre's %.5f %.5f",
                         (double)curves[c], (double)xs[i], du, dv, du0, dv0);
            }
        }
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(fov_viture_pro),
        cmocka_unit_test(fov_xreal),
        cmocka_unit_test(fov_zoom_2),
        cmocka_unit_test(uv_forward),
        cmocka_unit_test(uv_right_edge),
        cmocka_unit_test(uv_left_edge),
        cmocka_unit_test(uv_up),
        cmocka_unit_test(uv_down),
        cmocka_unit_test(uv_behind),
        cmocka_unit_test(uv_just_behind),
        cmocka_unit_test(uv_unnormalised_input),
        cmocka_unit_test(uv_degenerate_rejected),
        cmocka_unit_test(uv_clamped_in_range),
        cmocka_unit_test(fov_forward_is_centre),
        cmocka_unit_test(fov_horizontal_edge),
        cmocka_unit_test(fov_vertical_edge),
        cmocka_unit_test(fov_partial_scales_uv),
        cmocka_unit_test(fov_behind_and_degenerate),
        cmocka_unit_test(fov_180_agrees_with_plain),
        cmocka_unit_test(fov_180_still_clamps_not_rejects),
        cmocka_unit_test(fov_partial_never_returns_out_of_range),
        cmocka_unit_test(fov_180_axis_never_rejects),
        cmocka_unit_test(eye_sbs_lr),
        cmocka_unit_test(eye_sbs_rl),
        cmocka_unit_test(eye_mono),
        cmocka_unit_test(eye_tb_lr),
        cmocka_unit_test(eye_tb_rl),
        cmocka_unit_test(layout_axis_predicates),
        cmocka_unit_test(packing_auto_rows),
        cmocka_unit_test(packing_explicit_and_mono),
        cmocka_unit_test(picture_aspect_table),
        cmocka_unit_test(picture_aspect_junk_is_16_9),
        cmocka_unit_test(flat_screen_sizes),
        cmocka_unit_test(flat_contain_at_s1),
        cmocka_unit_test(flat_magnify),
        cmocka_unit_test(hequirect_geometry),
        cmocka_unit_test(geometry_junk_inputs_stay_finite),
        cmocka_unit_test(flat_uv_centre_and_edges),
        cmocka_unit_test(flat_uv_behind_and_degenerate),
        cmocka_unit_test(flat_top_edge_does_not_bow),
        cmocka_unit_test(curve_off_is_the_plane),
        cmocka_unit_test(curve_uv_centre_edges_top_bottom),
        cmocka_unit_test(curve_behind_and_degenerate),
        cmocka_unit_test(curve_columns_straight_top_bows),
        cmocka_unit_test(curve_half_angle),
        cmocka_unit_test(minify_flat_screen),
        cmocka_unit_test(minify_flat_takes_larger_axis),
        cmocka_unit_test(minify_hequirect),
        cmocka_unit_test(minify_junk_is_zero),
        cmocka_unit_test(minify_curved_centre_is_the_plane),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
