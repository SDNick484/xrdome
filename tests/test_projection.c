#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <math.h>
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
    assert_float_equal(uv[1], 1.0f, 0.f);
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
        assert_in_range(uv[0], 0.f, 1.f);
        assert_in_range(uv[1], 0.f, 1.f);
    }
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
        cmocka_unit_test(uv_behind),
        cmocka_unit_test(uv_just_behind),
        cmocka_unit_test(uv_unnormalised_input),
        cmocka_unit_test(uv_degenerate_rejected),
        cmocka_unit_test(uv_clamped_in_range),
        cmocka_unit_test(eye_sbs_lr),
        cmocka_unit_test(eye_sbs_rl),
        cmocka_unit_test(eye_swap),
        cmocka_unit_test(eye_mono),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
