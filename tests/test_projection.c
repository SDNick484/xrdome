#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <cmocka.h>

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

static void check_eye(xrd_layout l, int eye, bool swap, float es, float eo)
{
    float s = -1.f, o = -1.f;
    xrd_eye_u_transform(l, eye, swap, &s, &o);
    assert_float_equal(s, es, TOL);
    assert_float_equal(o, eo, TOL);
}

static void eye_sbs_lr(void **state)
{
    (void)state;
    check_eye(XRD_LAYOUT_SBS_LR, 0, false, 0.5f, 0.0f);
    check_eye(XRD_LAYOUT_SBS_LR, 1, false, 0.5f, 0.5f);
}

static void eye_sbs_rl(void **state)
{
    (void)state;
    check_eye(XRD_LAYOUT_SBS_RL, 0, false, 0.5f, 0.5f);
    check_eye(XRD_LAYOUT_SBS_RL, 1, false, 0.5f, 0.0f);
}

static void eye_swap(void **state)
{
    (void)state;
    check_eye(XRD_LAYOUT_SBS_LR, 0, true, 0.5f, 0.5f);
    check_eye(XRD_LAYOUT_SBS_LR, 1, true, 0.5f, 0.0f);
    check_eye(XRD_LAYOUT_SBS_RL, 0, true, 0.5f, 0.0f);
}

static void eye_mono(void **state)
{
    (void)state;
    for (int eye = 0; eye < 2; eye++) {
        check_eye(XRD_LAYOUT_MONO, eye, false, 1.0f, 0.0f);
        check_eye(XRD_LAYOUT_MONO, eye, true, 1.0f, 0.0f);
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
        cmocka_unit_test(eye_swap),
        cmocka_unit_test(eye_mono),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
