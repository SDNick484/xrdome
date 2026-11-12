#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <math.h>
#include <cmocka.h>

#include "math/quat.h"

#define TOL 1e-5

static const float FWD[3] = {0.f, 0.f, -1.f};

static void assert_vec3(const float got[3], float x, float y, float z)
{
    assert_float_equal(got[0], x, TOL);
    assert_float_equal(got[1], y, TOL);
    assert_float_equal(got[2], z, TOL);
}

static void assert_quat_equal(xrd_quat a, xrd_quat b)
{
    assert_float_equal(a.w, b.w, TOL);
    assert_float_equal(a.x, b.x, TOL);
    assert_float_equal(a.y, b.y, TOL);
    assert_float_equal(a.z, b.z, TOL);
}

static void yaw_90_turns_left(void **state)
{
    (void)state;
    float out[3];
    xrd_quat_rotate(xrd_quat_from_ypr_deg(90, 0, 0), FWD, out);
    assert_vec3(out, -1.f, 0.f, 0.f);
}

static void pitch_90_looks_up(void **state)
{
    (void)state;
    float out[3];
    xrd_quat_rotate(xrd_quat_from_ypr_deg(0, 90, 0), FWD, out);
    assert_vec3(out, 0.f, 1.f, 0.f);
}

static void roll_keeps_forward(void **state)
{
    (void)state;
    float out[3];
    xrd_quat_rotate(xrd_quat_from_ypr_deg(0, 0, 37), FWD, out);
    assert_vec3(out, 0.f, 0.f, -1.f);
}

static void conj_inverts(void **state)
{
    (void)state;
    xrd_quat q = xrd_quat_from_ypr_deg(30, -20, 10);
    assert_quat_equal(xrd_quat_mul(q, xrd_quat_conj(q)), XRD_QUAT_IDENTITY);
}

static void pitch_pm90_is_finite(void **state)
{
    (void)state;
    const double pitches[2] = {90, -90};
    for (int i = 0; i < 2; i++) {
        xrd_quat q = xrd_quat_from_ypr_deg(45, pitches[i], 45);
        assert_true(isfinite(q.w) && isfinite(q.x) &&
                    isfinite(q.y) && isfinite(q.z));
        float len = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
        assert_float_equal(len, 1.f, TOL);
    }
}

static void normalize_zero(void **state)
{
    (void)state;
    assert_quat_equal(xrd_quat_normalize((xrd_quat){0, 0, 0, 0}),
                      XRD_QUAT_IDENTITY);
}

static void normalize_nan(void **state)
{
    (void)state;
    assert_quat_equal(xrd_quat_normalize((xrd_quat){NAN, 0, 0, 0}),
                      XRD_QUAT_IDENTITY);
}

static void yaw_only_drops_pitch(void **state)
{
    (void)state;
    xrd_quat got = xrd_quat_yaw_only(xrd_quat_from_ypr_deg(40, 25, 10));
    assert_quat_equal(got, xrd_quat_from_ypr_deg(40, 0, 0));
}

static void mat3_matches_rotate(void **state)
{
    (void)state;
    xrd_quat q = xrd_quat_from_ypr_deg(30, -20, 10);
    const float v[3] = {0.3f, -0.5f, 0.8f};
    float want[3], m[9];
    xrd_quat_rotate(q, v, want);
    xrd_quat_to_mat3(q, m);
    /* column-major: out[r] = sum_c m[c*3 + r] * v[c] */
    float got[3];
    for (int r = 0; r < 3; r++)
        got[r] = m[0 * 3 + r] * v[0] + m[1 * 3 + r] * v[1] + m[2 * 3 + r] * v[2];
    assert_vec3(got, want[0], want[1], want[2]);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(yaw_90_turns_left),
        cmocka_unit_test(pitch_90_looks_up),
        cmocka_unit_test(roll_keeps_forward),
        cmocka_unit_test(conj_inverts),
        cmocka_unit_test(pitch_pm90_is_finite),
        cmocka_unit_test(normalize_zero),
        cmocka_unit_test(normalize_nan),
        cmocka_unit_test(yaw_only_drops_pitch),
        cmocka_unit_test(mat3_matches_rotate),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
