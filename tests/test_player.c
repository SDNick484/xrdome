#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>
#include <cmocka.h>

#include "video/player_mpv.h"

static void size_fits_within_limit(void **state)
{
    (void)state;
    char err[256] = "";
    assert_true(xrd_video_size_fits(4096, 2048, 16384, err, sizeof err));
    assert_true(xrd_video_size_fits(16384, 16384, 16384, err, sizeof err));
}

static void size_fits_too_large_names_both(void **state)
{
    (void)state;
    char err[256] = "";
    assert_false(xrd_video_size_fits(20000, 10000, 16384, err, sizeof err));
    assert_non_null(strstr(err, "20000"));
    assert_non_null(strstr(err, "16384"));

    err[0] = '\0';
    assert_false(xrd_video_size_fits(8192, 16385, 16384, err, sizeof err));
    assert_non_null(strstr(err, "16385"));
    assert_non_null(strstr(err, "16384"));
}

static void size_fits_rejects_non_positive(void **state)
{
    (void)state;
    char err[256] = "";
    assert_false(xrd_video_size_fits(0, 2048, 16384, err, sizeof err));
    assert_true(strlen(err) > 0);
    assert_false(xrd_video_size_fits(4096, -1, 16384, err, sizeof err));
    /* NULL err is allowed */
    assert_false(xrd_video_size_fits(20000, 10000, 16384, NULL, 0));
}

static void sbs_2to1(void **state)
{
    (void)state;
    assert_true(xrd_video_is_sbs_2to1(4096, 2048));
    assert_true(xrd_video_is_sbs_2to1(5760, 2880));
    assert_false(xrd_video_is_sbs_2to1(1920, 1080));
    assert_false(xrd_video_is_sbs_2to1(2048, 2048));
    assert_false(xrd_video_is_sbs_2to1(0, 0));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(size_fits_within_limit),
        cmocka_unit_test(size_fits_too_large_names_both),
        cmocka_unit_test(size_fits_rejects_non_positive),
        cmocka_unit_test(sbs_2to1),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
