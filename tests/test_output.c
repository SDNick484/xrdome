#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "display/output.h"

static void make_outs(xrd_output_info o[2], int dp2_w)
{
    memset(o, 0, 2 * sizeof *o);
    strcpy(o[0].name, "eDP-1");
    o[0].w = 1920; o[0].h = 1200; o[0].refresh_hz = 60;
    strcpy(o[1].name, "DP-2");
    o[1].x = 1920; o[1].w = dp2_w; o[1].h = 1080; o[1].refresh_hz = 60;
}

static void test_auto_picks_sbs(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = -1;
    make_outs(o, 3840);
    assert_int_equal(xrd_output_select(o, 2, "auto", &idx), XRD_OUT_OK);
    assert_int_equal(idx, 1);
    idx = -1;
    assert_int_equal(xrd_output_select(o, 2, NULL, &idx), XRD_OUT_OK);
    assert_int_equal(idx, 1);
}

static void test_named_match(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = -1;
    make_outs(o, 3840);
    assert_int_equal(xrd_output_select(o, 2, "DP-2", &idx), XRD_OUT_OK);
    assert_int_equal(idx, 1);
}

static void test_named_not_sbs(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = 77;
    make_outs(o, 3840);
    assert_int_equal(xrd_output_select(o, 2, "eDP-1", &idx), XRD_OUT_NOT_SBS);
    assert_int_equal(idx, 77);
}

static void test_named_missing(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = 77;
    make_outs(o, 3840);
    assert_int_equal(xrd_output_select(o, 2, "HDMI-9", &idx), XRD_OUT_NOT_FOUND);
    assert_int_equal(idx, 77);
}

static void test_auto_wrong_size(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = 77;
    make_outs(o, 1920);
    assert_int_equal(xrd_output_select(o, 2, "auto", &idx), XRD_OUT_NOT_FOUND);
    assert_int_equal(idx, 77);
}

static void test_empty(void **s)
{
    (void)s;
    int idx = 77;
    assert_int_equal(xrd_output_select(NULL, 0, "auto", &idx), XRD_OUT_NOT_FOUND);
    assert_int_equal(idx, 77);
}

static void test_strerror(void **s)
{
    (void)s;
    assert_string_equal(xrd_output_strerror(XRD_OUT_OK, "auto"), "ok");
    assert_non_null(strstr(xrd_output_strerror(XRD_OUT_NOT_SBS, "eDP-1"),
                           "switch the glasses to SBS"));
    const char *m = xrd_output_strerror(XRD_OUT_NOT_FOUND, "auto");
    assert_non_null(strstr(m, "3840x1080"));
    assert_non_null(strstr(m, "SBS"));
    assert_non_null(strstr(xrd_output_strerror(XRD_OUT_NOT_FOUND, "HDMI-9"), "HDMI-9"));
}

int main(void)
{
    const struct CMUnitTest t[] = {
        cmocka_unit_test(test_auto_picks_sbs),
        cmocka_unit_test(test_named_match),
        cmocka_unit_test(test_named_not_sbs),
        cmocka_unit_test(test_named_missing),
        cmocka_unit_test(test_auto_wrong_size),
        cmocka_unit_test(test_empty),
        cmocka_unit_test(test_strerror),
    };
    return cmocka_run_group_tests(t, NULL, NULL);
}
