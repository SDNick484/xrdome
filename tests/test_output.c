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

static void make_sdl_outs(xrd_output_info o[2])
{
    memset(o, 0, 2 * sizeof *o);
    strcpy(o[0].name, "DP-2 14\"");
    o[0].w = 1920; o[0].h = 1200;
    strcpy(o[1].name, "VITURE 23\"");
    o[1].x = 1920; o[1].w = 3840; o[1].h = 1080;
}

static void test_prefix_matches_before_space(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = 77;
    make_sdl_outs(o);
    assert_int_equal(xrd_output_select(o, 2, "VITURE", &idx), XRD_OUT_OK);
    assert_int_equal(idx, 1);
    idx = 77;
    assert_int_equal(xrd_output_select(o, 2, "VITURE 23\"", &idx), XRD_OUT_OK);
    assert_int_equal(idx, 1);
}

static void test_prefix_needs_space(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = 77;
    make_sdl_outs(o);
    assert_int_equal(xrd_output_select(o, 2, "DP", &idx), XRD_OUT_NOT_FOUND);
    assert_int_equal(idx, 77);
}

static void test_prefix_match_not_sbs(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = 77;
    make_sdl_outs(o);
    assert_int_equal(xrd_output_select(o, 2, "DP-2", &idx), XRD_OUT_NOT_SBS);
    assert_int_equal(idx, 77);
}

static void test_first_match_wins(void **s)
{
    (void)s;
    xrd_output_info o[2];
    int idx = 77;
    make_sdl_outs(o);
    strcpy(o[0].name, "VITURE 14\"");
    o[0].w = 3840; o[0].h = 1080;
    assert_int_equal(xrd_output_select(o, 2, "VITURE", &idx), XRD_OUT_OK);
    assert_int_equal(idx, 0);
}

static void test_list(void **s)
{
    (void)s;
    xrd_output_info o[2];
    char buf[128];
    make_sdl_outs(o);
    xrd_output_list(o, 2, buf, sizeof buf);
    assert_string_equal(buf, "DP-2 14\" (1920x1200), VITURE 23\" (3840x1080)");
    xrd_output_list(o, 0, buf, sizeof buf);
    assert_string_equal(buf, "none");
    char small[10];
    memset(small, 'x', sizeof small);
    xrd_output_list(o, 2, small, sizeof small);
    assert_int_equal(small[sizeof small - 1], '\0');
    assert_true(strlen(small) < sizeof small);
    char one[1] = {'x'};
    xrd_output_list(o, 2, one, sizeof one);
    assert_int_equal(one[0], '\0');
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
        cmocka_unit_test(test_prefix_matches_before_space),
        cmocka_unit_test(test_prefix_needs_space),
        cmocka_unit_test(test_prefix_match_not_sbs),
        cmocka_unit_test(test_first_match_wins),
        cmocka_unit_test(test_list),
    };
    return cmocka_run_group_tests(t, NULL, NULL);
}
