/* libxrdome-core links on its own: this program is built against
 * xrdome_core_dep and cmocka only (meson.build), with no SDL, mpv or EGL, and
 * calls into each part of the core so the linker pulls it in. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "app/controls.h"
#include "detect/detect.h"
#include "devices.h"
#include "math/quat.h"
#include "pose/pose.h"
#include "render/projection.h"
#include "store/filestate.h"
#include "track/tracker.h"
#include "values.h"

#define TOL 1e-5

static void quat_identity(void **state)
{
    (void)state;
    xrd_quat q = xrd_quat_from_ypr_deg(0, 0, 0);
    assert_float_equal(q.w, 1.f, TOL);
    assert_float_equal(q.x, 0.f, TOL);
    assert_float_equal(q.y, 0.f, TOL);
    assert_float_equal(q.z, 0.f, TOL);
}

static void projection_forward(void **state)
{
    (void)state;
    const float d[3] = {0.f, 0.f, -1.f};
    float uv[2];
    assert_true(xrd_dir_to_uv(d, uv));
    assert_float_equal(uv[0], 0.5f, TOL);
    assert_float_equal(uv[1], 0.5f, TOL);
}

static void values_layout(void **state)
{
    (void)state;
    xrd_layout l = XRD_LAYOUT_MONO;
    assert_true(xrd_value_layout("sbs-lr", &l));
    assert_int_equal(l, XRD_LAYOUT_SBS_LR);
}

static void detect_shape(void **state)
{
    (void)state;
    xrd_probe p;
    xrd_probe_zero(&p);
    p.have_size = true;
    p.width = 2880;
    p.height = 1440; /* 2:1, the shape of hequirect sbs-lr */
    xrd_detect d;
    xrd_detect_resolve(NULL, NULL, &p, "film.mkv", &d);
    assert_int_equal(d.proj, XRD_PROJ_HEQUIRECT);
    assert_int_equal(d.layout, XRD_LAYOUT_SBS_LR);
    assert_int_equal(d.layout_src, XRD_SRC_SHAPE);
}

static void tracker_null(void **state)
{
    (void)state;
    char err[256] = "";
    xrd_pose_source *src = xrd_pose_source_create("null", err, sizeof err);
    assert_non_null(src);
    xrd_tracker *t = xrd_tracker_new(src);
    assert_non_null(t);
    xrd_tracker_free(t);
    xrd_pose_source_destroy(src);
}

static void filestate_fnv(void **state)
{
    (void)state;
    assert_true(xrd_fnv1a64(XRD_FNV1A64_INIT, "a", 1) == 0xaf63dc4c8601ec8cULL);
}

static void devices_builtin(void **state)
{
    (void)state;
    float f = 0.f;
    assert_true(xrd_device_builtin("viture-pro", &f));
    assert_true(f == 43.0f);
}

static void controls_zoom(void **state)
{
    (void)state;
    xrd_controls c;
    memset(&c, 0, sizeof c);
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_zoom_in(&c);
    assert_float_equal(c.zoom, 1.1f, TOL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(quat_identity),
        cmocka_unit_test(projection_forward),
        cmocka_unit_test(values_layout),
        cmocka_unit_test(detect_shape),
        cmocka_unit_test(tracker_null),
        cmocka_unit_test(filestate_fnv),
        cmocka_unit_test(devices_builtin),
        cmocka_unit_test(controls_zoom),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
