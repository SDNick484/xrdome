#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "math/quat.h"
#include "pose/pose.h"
#include "track/tracker.h"

#define TOL 1e-4
#define MS 1000000ull

typedef struct {
    xrd_pose_source base;
    int has;
    xrd_pose pose;
} fake_src;

static int fake_latest(xrd_pose_source *s, xrd_pose *out)
{
    fake_src *f = (fake_src *)s;
    if (!f->has)
        return 1;
    *out = f->pose;
    return 0;
}

static void fake_init(fake_src *f)
{
    memset(f, 0, sizeof *f);
    f->base.name = "fake";
    f->base.latest = fake_latest;
}

static void fake_set(fake_src *f, xrd_quat q, uint64_t t)
{
    f->has = 1;
    f->pose.q = q;
    f->pose.t_mono_ns = t;
    f->pose.seq++;
}

static xrd_quat ypr(double a, double b, double c)
{
    return xrd_quat_from_ypr_deg(a, b, c);
}

/* Same rotation (q and -q included): compare the images of two
 * non-parallel vectors. */
static void assert_same_rotation(xrd_quat a, xrd_quat b)
{
    const float v1[3] = {1, 0, 0};
    const float v2[3] = {0, 0.6f, -0.8f};
    const float *vs[2] = {v1, v2};
    for (int i = 0; i < 2; i++) {
        float ra[3], rb[3];
        xrd_quat_rotate(a, vs[i], ra);
        xrd_quat_rotate(b, vs[i], rb);
        for (int k = 0; k < 3; k++)
            assert_true(fabs((double)ra[k] - (double)rb[k]) < TOL);
    }
}

typedef struct {
    int stale, resumed, nodata, other;
} log_counts;

static void count_sink(const char *msg, void *user)
{
    log_counts *c = user;
    if (strstr(msg, "pose stale"))
        c->stale++;
    else if (strstr(msg, "pose resumed"))
        c->resumed++;
    else if (strstr(msg, "no pose data"))
        c->nodata++;
    else
        c->other++;
}

static void test_no_data_is_identity(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    assert_non_null(t);
    assert_same_rotation(xrd_tracker_view(t, 1000), XRD_QUAT_IDENTITY);
    assert_false(xrd_tracker_is_stale(t));
    xrd_tracker_free(t);
}

static void test_no_data_logs_once(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    log_counts c = {0};
    xrd_tracker *t = xrd_tracker_new(&f.base);
    xrd_tracker_set_log(t, count_sink, &c);
    for (int i = 0; i < 3; i++)
        xrd_tracker_view(t, 1000 + (uint64_t)i);
    assert_int_equal(c.nodata, 1);
    assert_int_equal(c.other, 0);
    xrd_tracker_free(t);
}

static void test_first_pose_recenters(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    fake_set(&f, ypr(30, 0, 0), 100);
    assert_same_rotation(xrd_tracker_view(t, 100), XRD_QUAT_IDENTITY);
    xrd_tracker_free(t);
}

static void test_relative_after_first(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    fake_set(&f, ypr(30, 0, 0), 100);
    xrd_tracker_view(t, 100);
    fake_set(&f, ypr(50, 0, 0), 110);
    assert_same_rotation(xrd_tracker_view(t, 110), ypr(20, 0, 0));
    xrd_tracker_free(t);
}

static void test_recenter_before_pose_is_noop(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    xrd_tracker_recenter(t, false);
    xrd_tracker_recenter(t, true);
    fake_set(&f, ypr(30, 0, 0), 100);
    assert_same_rotation(xrd_tracker_view(t, 100), XRD_QUAT_IDENTITY);
    fake_set(&f, ypr(50, 0, 0), 110);
    assert_same_rotation(xrd_tracker_view(t, 110), ypr(20, 0, 0));
    xrd_tracker_free(t);
}

/* Scenario runners take the world frame W applied to every raw pose. */
static void run_recenter_full(xrd_quat W)
{
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    fake_set(&f, xrd_quat_mul(W, ypr(30, 0, 0)), 100);
    xrd_tracker_view(t, 100);
    fake_set(&f, xrd_quat_mul(W, ypr(30, 10, 5)), 110);
    xrd_tracker_view(t, 110);
    xrd_tracker_recenter(t, false);
    assert_same_rotation(xrd_tracker_view(t, 110), XRD_QUAT_IDENTITY);
    xrd_tracker_free(t);
}

static void run_recenter_yaw_only(xrd_quat W)
{
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    fake_set(&f, xrd_quat_mul(W, XRD_QUAT_IDENTITY), 100);
    xrd_tracker_view(t, 100);
    fake_set(&f, xrd_quat_mul(W, ypr(30, 10, 0)), 110);
    xrd_tracker_view(t, 110);
    xrd_tracker_recenter(t, true);
    assert_same_rotation(xrd_tracker_view(t, 110), ypr(0, 10, 0));
    xrd_tracker_free(t);
}

static void recenter_yaw_only_from_tilted_ref(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    xrd_quat q0 = ypr(30, 10, 5), q1 = ypr(70, 20, 5);
    fake_set(&f, q0, 100);
    xrd_tracker_view(t, 100); /* first pose: q_ref = q0 */
    fake_set(&f, q1, 110);
    xrd_tracker_view(t, 110);
    xrd_tracker_recenter(t, true);

    xrd_quat yaw = xrd_quat_yaw_only(xrd_quat_mul(xrd_quat_conj(q0), q1));
    xrd_quat ref_new = xrd_quat_mul(q0, yaw);
    xrd_quat want = xrd_quat_mul(xrd_quat_conj(ref_new), q1);
    xrd_quat got = xrd_tracker_view(t, 110);
    assert_same_rotation(got, want);

    /* The wrong composition order (yaw * q_ref) must be distinguishable. */
    xrd_quat wrong_ref = xrd_quat_mul(yaw, q0);
    xrd_quat wrong = xrd_quat_mul(xrd_quat_conj(wrong_ref), q1);
    const float v[3] = {1, 0, 0};
    float a[3], b[3];
    xrd_quat_rotate(got, v, a);
    xrd_quat_rotate(wrong, v, b);
    double dot = (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
    if (dot > 1.0)
        dot = 1.0;
    if (dot < -1.0)
        dot = -1.0;
    assert_true(acos(dot) * 180.0 / 3.14159265358979323846 > 1.0);
    xrd_tracker_free(t);
}

static void run_relative(xrd_quat W)
{
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    fake_set(&f, xrd_quat_mul(W, ypr(30, 0, 0)), 100);
    assert_same_rotation(xrd_tracker_view(t, 100), XRD_QUAT_IDENTITY);
    fake_set(&f, xrd_quat_mul(W, ypr(50, 0, 0)), 110);
    assert_same_rotation(xrd_tracker_view(t, 110), ypr(20, 0, 0));
    xrd_tracker_free(t);
}

static void test_recenter_full(void **st)
{
    (void)st;
    run_recenter_full(XRD_QUAT_IDENTITY);
}

static void test_recenter_yaw_only(void **st)
{
    (void)st;
    run_recenter_yaw_only(XRD_QUAT_IDENTITY);
}

static void test_world_frame_invariant(void **st)
{
    (void)st;
    xrd_quat W = ypr(-157, 0, 180);
    run_relative(W);
    run_recenter_full(W);
    run_recenter_yaw_only(W);
}

static void test_stale_holds_last(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    uint64_t t0 = 1000 * MS;
    fake_set(&f, ypr(30, 0, 0), t0);
    xrd_tracker_view(t, t0);
    fake_set(&f, ypr(50, 0, 0), t0 + 10 * MS);
    xrd_quat v = xrd_tracker_view(t, t0 + 10 * MS);
    assert_false(xrd_tracker_is_stale(t));
    assert_int_equal(xrd_tracker_last_pose_ns(t), t0 + 10 * MS);
    xrd_quat held = xrd_tracker_view(t, t0 + 10 * MS + 300 * MS);
    assert_same_rotation(held, v);
    assert_same_rotation(held, ypr(20, 0, 0));
    assert_true(xrd_tracker_is_stale(t));
    xrd_tracker_free(t);
}

static void test_stale_boundary(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    fake_set(&f, ypr(30, 0, 0), 1000);
    xrd_tracker_view(t, 1000 + XRD_STALE_NS);
    assert_false(xrd_tracker_is_stale(t)); /* strictly greater than */
    xrd_tracker_view(t, 1000 + XRD_STALE_NS + 1);
    assert_true(xrd_tracker_is_stale(t));
    xrd_tracker_free(t);
}

static void test_fresh_clears_stale(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    uint64_t t0 = 1000 * MS;
    fake_set(&f, ypr(30, 0, 0), t0);
    xrd_tracker_view(t, t0);
    xrd_tracker_view(t, t0 + 300 * MS);
    assert_true(xrd_tracker_is_stale(t));
    fake_set(&f, ypr(60, 0, 0), t0 + 310 * MS);
    xrd_quat v = xrd_tracker_view(t, t0 + 310 * MS);
    assert_false(xrd_tracker_is_stale(t));
    assert_same_rotation(v, ypr(30, 0, 0));
    xrd_tracker_free(t);
}

static void test_stale_logs_once(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    log_counts c = {0};
    xrd_tracker *t = xrd_tracker_new(&f.base);
    xrd_tracker_set_log(t, count_sink, &c);
    uint64_t t0 = 1000 * MS;
    fake_set(&f, ypr(30, 0, 0), t0);
    xrd_tracker_view(t, t0);
    xrd_tracker_view(t, t0 + 300 * MS);
    xrd_tracker_view(t, t0 + 320 * MS);
    xrd_tracker_view(t, t0 + 340 * MS);
    assert_int_equal(c.stale, 1);
    assert_int_equal(c.resumed, 0);
    fake_set(&f, ypr(40, 0, 0), t0 + 350 * MS);
    xrd_tracker_view(t, t0 + 350 * MS);
    xrd_tracker_view(t, t0 + 360 * MS);
    assert_int_equal(c.resumed, 1);
    assert_int_equal(c.stale, 1);
    assert_int_equal(c.nodata, 0);
    assert_int_equal(c.other, 0);
    xrd_tracker_free(t);
}

static void test_recenter_while_stale_updates_held_view(void **st)
{
    (void)st;
    fake_src f;
    fake_init(&f);
    xrd_tracker *t = xrd_tracker_new(&f.base);
    fake_set(&f, ypr(30, 0, 0), 1000);
    xrd_tracker_view(t, 1000);
    fake_set(&f, ypr(50, 0, 0), 1010);
    xrd_tracker_view(t, 1010);
    xrd_tracker_view(t, 1010 + 300 * MS);
    assert_true(xrd_tracker_is_stale(t));
    xrd_tracker_recenter(t, false);
    assert_same_rotation(xrd_tracker_view(t, 1010 + 310 * MS),
                         XRD_QUAT_IDENTITY);
    xrd_tracker_free(t);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_no_data_is_identity),
        cmocka_unit_test(test_no_data_logs_once),
        cmocka_unit_test(test_first_pose_recenters),
        cmocka_unit_test(test_relative_after_first),
        cmocka_unit_test(test_recenter_before_pose_is_noop),
        cmocka_unit_test(test_recenter_full),
        cmocka_unit_test(test_recenter_yaw_only),
        cmocka_unit_test(test_world_frame_invariant),
        cmocka_unit_test(recenter_yaw_only_from_tilted_ref),
        cmocka_unit_test(test_stale_holds_last),
        cmocka_unit_test(test_stale_boundary),
        cmocka_unit_test(test_fresh_clears_stale),
        cmocka_unit_test(test_stale_logs_once),
        cmocka_unit_test(test_recenter_while_stale_updates_held_view),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
