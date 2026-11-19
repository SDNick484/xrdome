#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <cmocka.h>

#include "convert.h"

#define TOL 1e-4

typedef struct { double w, x, y, z; } q4;

static q4 mul(q4 a, q4 b)
{
    return (q4){a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

static q4 axis(double ax, double ay, double az, double deg)
{
    double h = deg * 3.14159265358979323846 / 360.0;
    return (q4){cos(h), ax * sin(h), ay * sin(h), az * sin(h)};
}

/* Rz(yaw) * Ry(pitch) * Rx(roll) in NWU, built from axis-angle pieces. */
static q4 from_ypr(double yaw, double pitch, double roll)
{
    return mul(mul(axis(0, 0, 1, yaw), axis(0, 1, 0, pitch)), axis(1, 0, 0, roll));
}

static void to_f(q4 q, float out[4])
{
    out[0] = (float)q.w;
    out[1] = (float)q.x;
    out[2] = (float)q.y;
    out[3] = (float)q.z;
}

static void expect(q4 q, double yaw, double pitch, double roll)
{
    float f[4];
    double ypr[3];
    to_f(q, f);
    assert_int_equal(viture_quat_to_ypr(f, ypr), 0);
    assert_float_equal(ypr[0], yaw, TOL * 10);
    assert_float_equal(ypr[1], pitch, TOL * 10);
    assert_float_equal(ypr[2], roll, TOL * 10);
}

static void identity(void **state)
{
    (void)state;
    expect((q4){1, 0, 0, 0}, 0, 0, 0);
}

static void single_axes(void **state)
{
    (void)state;
    /* A turn to the left (positive about up) is positive yaw. */
    expect(axis(0, 0, 1, 90), 90, 0, 0);
    expect(axis(0, 0, 1, -35), -35, 0, 0);
    /* Positive about west: the nose goes down. */
    expect(axis(0, 1, 0, 30), 0, 30, 0);
    /* Positive about north (forward): the right ear goes down. */
    expect(axis(1, 0, 0, 20), 0, 0, 20);
}

static void known_quaternions(void **state)
{
    (void)state;
    /* 90 degrees about up: w = x = 0.7071 split as w and z. */
    float q[4] = {0.70710678f, 0, 0, 0.70710678f};
    double ypr[3];
    assert_int_equal(viture_quat_to_ypr(q, ypr), 0);
    assert_float_equal(ypr[0], 90, TOL);
    assert_float_equal(ypr[1], 0, TOL);
    assert_float_equal(ypr[2], 0, TOL);

    /* 180 about up gives yaw 180 (or -180), nothing else. */
    float h[4] = {0, 0, 0, 1};
    assert_int_equal(viture_quat_to_ypr(h, ypr), 0);
    assert_float_equal(fabs(ypr[0]), 180, TOL);
    assert_float_equal(ypr[1], 0, TOL);
    assert_float_equal(ypr[2], 0, TOL);
}

static void combined_angles_round_trip(void **state)
{
    (void)state;
    for (double yaw = -170; yaw <= 170; yaw += 34)
        for (double pitch = -80; pitch <= 80; pitch += 20)
            for (double roll = -170; roll <= 170; roll += 34)
                expect(from_ypr(yaw, pitch, roll), yaw, pitch, roll);
}

/* v rotated by unit q: q * (0, v) * conj(q). */
static void rotate(q4 q, const double v[3], double out[3])
{
    q4 p = mul(mul(q, (q4){0, v[0], v[1], v[2]}), (q4){q.w, -q.x, -q.y, -q.z});
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

static void axes_mean_what_the_reader_expects(void **state)
{
    (void)state;
    /* NWU: x north (forward), y west (left), z up. Yaw 90 turns the nose to
     * the left, then pitch 45 puts it down: forward ends up pointing west
     * and below the horizon. */
    float f[4];
    double ypr[3], fwd[3] = {1, 0, 0}, out[3];
    to_f(from_ypr(90, 45, 0), f);
    assert_int_equal(viture_quat_to_ypr(f, ypr), 0);
    assert_float_equal(ypr[0], 90, TOL * 10);
    assert_float_equal(ypr[1], 45, TOL * 10);
    rotate(from_ypr(ypr[0], ypr[1], ypr[2]), fwd, out);
    assert_float_equal(out[0], 0, TOL);
    assert_float_equal(out[1], sqrt(0.5), TOL);
    assert_float_equal(out[2], -sqrt(0.5), TOL);
}

static void sign_of_quaternion_is_irrelevant(void **state)
{
    (void)state;
    q4 q = from_ypr(50, -20, 10);
    q4 n = {-q.w, -q.x, -q.y, -q.z};
    float a[4], b[4];
    double ya[3], yb[3];
    to_f(q, a);
    to_f(n, b);
    assert_int_equal(viture_quat_to_ypr(a, ya), 0);
    assert_int_equal(viture_quat_to_ypr(b, yb), 0);
    for (int i = 0; i < 3; i++)
        assert_float_equal(ya[i], yb[i], TOL);
}

static void length_is_normalised(void **state)
{
    (void)state;
    q4 q = from_ypr(-120, 33, 77);
    q4 big = {q.w * 3, q.x * 3, q.y * 3, q.z * 3};
    expect(big, -120, 33, 77);
}

static void straight_up_and_down_stay_finite(void **state)
{
    (void)state;
    float f[4];
    double ypr[3];
    to_f(from_ypr(0, 90, 0), f);
    assert_int_equal(viture_quat_to_ypr(f, ypr), 0);
    assert_float_equal(ypr[1], 90, 1e-2);
    assert_true(isfinite(ypr[0]) && isfinite(ypr[2]));
    /* A hair past 1 from rounding must not produce NaN. */
    float over[4] = {0.70710680f, 0, 0.70710680f, 0};
    assert_int_equal(viture_quat_to_ypr(over, ypr), 0);
    assert_true(isfinite(ypr[1]));
    assert_true(ypr[1] <= 90.0);
}

static void unusable_input_is_refused(void **state)
{
    (void)state;
    double ypr[3] = {1, 2, 3};
    float zero[4] = {0, 0, 0, 0};
    float nan_q[4] = {NAN, 0, 0, 1};
    float inf_q[4] = {1, INFINITY, 0, 0};
    assert_int_equal(viture_quat_to_ypr(zero, ypr), -1);
    assert_int_equal(viture_quat_to_ypr(nan_q, ypr), -1);
    assert_int_equal(viture_quat_to_ypr(inf_q, ypr), -1);
    assert_float_equal(ypr[0], 1, 0);
    assert_float_equal(ypr[1], 2, 0);
    assert_float_equal(ypr[2], 3, 0);
}

static void callback_layout_uses_the_quaternion(void **state)
{
    (void)state;
    /* roll, pitch, yaw (whatever unit), then w, x, y, z: the first three
     * floats are not read. */
    float data[7] = {999, -999, 123, 0.70710678f, 0, 0, 0.70710678f};
    double ypr[3];
    assert_int_equal(viture_pose_to_ypr(data, ypr), 0);
    assert_float_equal(ypr[0], 90, TOL);
    assert_float_equal(ypr[1], 0, TOL);
    assert_float_equal(ypr[2], 0, TOL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(identity),
        cmocka_unit_test(single_axes),
        cmocka_unit_test(known_quaternions),
        cmocka_unit_test(combined_angles_round_trip),
        cmocka_unit_test(axes_mean_what_the_reader_expects),
        cmocka_unit_test(sign_of_quaternion_is_irrelevant),
        cmocka_unit_test(length_is_normalised),
        cmocka_unit_test(straight_up_and_down_stay_finite),
        cmocka_unit_test(unusable_input_is_refused),
        cmocka_unit_test(callback_layout_uses_the_quaternion),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
