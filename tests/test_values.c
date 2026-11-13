#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>
#include <cmocka.h>

#include "values.h"

static void ranged_boundaries(void **state)
{
    (void)state;
    float v = 0;
    assert_int_equal(xrd_value_ranged("0.25", XRD_ZOOM_MIN, XRD_ZOOM_MAX, &v), XRD_VALUE_OK);
    assert_float_equal(v, 0.25f, 0);
    assert_int_equal(xrd_value_ranged("4", XRD_ZOOM_MIN, XRD_ZOOM_MAX, &v), XRD_VALUE_OK);
    assert_float_equal(v, 4.0f, 0);
    assert_int_equal(xrd_value_ranged("-5", -XRD_CONVERGENCE_MAX, XRD_CONVERGENCE_MAX, &v),
                     XRD_VALUE_OK);
    assert_float_equal(v, -5.0f, 0);
    assert_int_equal(xrd_value_ranged("5", -XRD_CONVERGENCE_MAX, XRD_CONVERGENCE_MAX, &v),
                     XRD_VALUE_OK);
    assert_float_equal(v, 5.0f, 0);
}

static void ranged_rejects(void **state)
{
    (void)state;
    float v = 7;
    assert_int_equal(xrd_value_ranged("0.2499", XRD_ZOOM_MIN, XRD_ZOOM_MAX, &v), XRD_VALUE_RANGE);
    assert_int_equal(xrd_value_ranged("4.0001", XRD_ZOOM_MIN, XRD_ZOOM_MAX, &v), XRD_VALUE_RANGE);
    assert_int_equal(xrd_value_ranged("-5.01", -XRD_CONVERGENCE_MAX, XRD_CONVERGENCE_MAX, &v),
                     XRD_VALUE_RANGE);
    assert_int_equal(xrd_value_ranged("abc", 0, 1, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_ranged("", 0, 1, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_ranged("1x", 0, 1, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_ranged("nan", 0, 1, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_ranged("inf", 0, 1, &v), XRD_VALUE_SYNTAX);
    assert_float_equal(v, 7.0f, 0); /* untouched on failure */
}

static void float_basic(void **state)
{
    (void)state;
    float v = 0;
    assert_true(xrd_value_float("1.5", &v));
    assert_float_equal(v, 1.5f, 0);
    assert_false(xrd_value_float("1.5 ", &v));
    assert_false(xrd_value_float("1e999", &v));
}

static void fov_accepts(void **state)
{
    (void)state;
    float h = 0, v = 0;
    assert_int_equal(xrd_value_content_fov("120x90", &h, &v), XRD_VALUE_OK);
    assert_float_equal(h, 120.0f, 0);
    assert_float_equal(v, 90.0f, 0);
    assert_int_equal(xrd_value_content_fov("10x180", &h, &v), XRD_VALUE_OK);
    assert_float_equal(h, 10.0f, 0);
    assert_float_equal(v, 180.0f, 0);
    assert_int_equal(xrd_value_content_fov("180x10", &h, &v), XRD_VALUE_OK);
    assert_int_equal(xrd_value_content_fov("90.5x60.25", &h, &v), XRD_VALUE_OK);
}

static void fov_rejects(void **state)
{
    (void)state;
    float h = 1, v = 2;
    assert_int_equal(xrd_value_content_fov("9.99x90", &h, &v), XRD_VALUE_RANGE);
    assert_int_equal(xrd_value_content_fov("90x180.01", &h, &v), XRD_VALUE_RANGE);
    assert_int_equal(xrd_value_content_fov("200x90", &h, &v), XRD_VALUE_RANGE);
    assert_int_equal(xrd_value_content_fov("120", &h, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_content_fov("x90", &h, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_content_fov("120x", &h, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_content_fov("-120x90", &h, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_content_fov("120x-90", &h, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_content_fov(" 120x90", &h, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_content_fov("0x1Ax90", &h, &v), XRD_VALUE_SYNTAX);
    assert_int_equal(xrd_value_content_fov("axb", &h, &v), XRD_VALUE_SYNTAX);
    assert_float_equal(h, 1.0f, 0);
    assert_float_equal(v, 2.0f, 0);
}

static void layout_round_trip(void **state)
{
    (void)state;
    xrd_layout l;
    assert_true(xrd_value_layout("sbs-lr", &l));
    assert_int_equal(l, XRD_LAYOUT_SBS_LR);
    assert_true(xrd_value_layout("sbs-rl", &l));
    assert_int_equal(l, XRD_LAYOUT_SBS_RL);
    assert_true(xrd_value_layout("mono", &l));
    assert_int_equal(l, XRD_LAYOUT_MONO);
    assert_false(xrd_value_layout("tb", &l));
    assert_false(xrd_value_layout("", &l));
    assert_false(xrd_value_layout("MONO", &l));
    assert_string_equal(xrd_value_layout_name(XRD_LAYOUT_SBS_LR), "sbs-lr");
    assert_string_equal(xrd_value_layout_name(XRD_LAYOUT_SBS_RL), "sbs-rl");
    assert_string_equal(xrd_value_layout_name(XRD_LAYOUT_MONO), "mono");
    assert_null(xrd_value_layout_name((xrd_layout)99));
}

static void yesno(void **state)
{
    (void)state;
    bool b = false;
    assert_true(xrd_value_yesno("yes", &b));
    assert_true(b);
    assert_true(xrd_value_yesno("no", &b));
    assert_false(b);
    assert_false(xrd_value_yesno("maybe", &b));
    assert_false(xrd_value_yesno("Yes", &b));
    assert_false(xrd_value_yesno("", &b));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(ranged_boundaries), cmocka_unit_test(ranged_rejects),
        cmocka_unit_test(float_basic),       cmocka_unit_test(fov_accepts),
        cmocka_unit_test(fov_rejects),       cmocka_unit_test(layout_round_trip),
        cmocka_unit_test(yesno),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
