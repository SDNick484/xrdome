#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>
#include <cmocka.h>

#include "platform/platform.h"

static void current_matches_build(void **state)
{
    (void)state;
#if defined(__APPLE__)
    assert_int_equal(xrd_platform_current(), XRD_PLATFORM_MACOS);
#elif defined(__linux__)
    assert_int_equal(xrd_platform_current(), XRD_PLATFORM_LINUX);
#endif
    assert_non_null(xrd_platform_name(xrd_platform_current()));
}

static void linux_unchanged(void **state)
{
    (void)state;
    /* Linux keeps exactly what it did before the platform module. */
    assert_string_equal(xrd_platform_name(XRD_PLATFORM_LINUX), "linux");
    assert_string_equal(xrd_platform_default_hwdec(XRD_PLATFORM_LINUX), "vaapi");
    assert_true(xrd_platform_uses_x11(XRD_PLATFORM_LINUX));
    assert_false(xrd_platform_gl_forward_compat(XRD_PLATFORM_LINUX));
    assert_false(xrd_platform_force_fullscreen(XRD_PLATFORM_LINUX));
    assert_true(xrd_platform_has_xrdriver(XRD_PLATFORM_LINUX));
}

static void macos_choices(void **state)
{
    (void)state;
    assert_string_equal(xrd_platform_name(XRD_PLATFORM_MACOS), "macos");
    assert_string_equal(xrd_platform_default_hwdec(XRD_PLATFORM_MACOS), "videotoolbox");
    assert_false(xrd_platform_uses_x11(XRD_PLATFORM_MACOS));
    assert_true(xrd_platform_gl_forward_compat(XRD_PLATFORM_MACOS));
    assert_true(xrd_platform_force_fullscreen(XRD_PLATFORM_MACOS));
    assert_false(xrd_platform_has_xrdriver(XRD_PLATFORM_MACOS));
}

static void other_defaults(void **state)
{
    (void)state;
    assert_string_equal(xrd_platform_default_hwdec(XRD_PLATFORM_OTHER), "auto-safe");
    assert_false(xrd_platform_has_xrdriver(XRD_PLATFORM_OTHER));
}

static void zero_copy(void **state)
{
    (void)state;
    assert_true(xrd_hwdec_is_zero_copy("vaapi"));
    assert_true(xrd_hwdec_is_zero_copy("nvdec"));
    assert_true(xrd_hwdec_is_zero_copy("cuda"));
    assert_true(xrd_hwdec_is_zero_copy("videotoolbox"));
    assert_false(xrd_hwdec_is_zero_copy("vaapi-copy"));
    assert_false(xrd_hwdec_is_zero_copy("videotoolbox-copy"));
    assert_false(xrd_hwdec_is_zero_copy("no"));
    assert_false(xrd_hwdec_is_zero_copy(""));
    assert_false(xrd_hwdec_is_zero_copy(NULL));
    assert_true(xrd_hwdec_is_zero_copy("copy")); /* not a "-copy" suffix */
}

static void name_valid(void **state)
{
    (void)state;
    assert_true(xrd_hwdec_name_valid("vaapi"));
    assert_true(xrd_hwdec_name_valid("auto-safe"));
    assert_true(xrd_hwdec_name_valid("nvdec,vaapi"));
    assert_true(xrd_hwdec_name_valid("d3d11va"));
    assert_false(xrd_hwdec_name_valid(""));
    assert_false(xrd_hwdec_name_valid(NULL));
    assert_false(xrd_hwdec_name_valid("VAAPI"));
    assert_false(xrd_hwdec_name_valid("vaapi;rm"));
    assert_false(xrd_hwdec_name_valid("vaapi copy"));
    assert_false(xrd_hwdec_name_valid("abcdefghijklmnopqrstuvwxyz012345")); /* 32 */
    assert_true(xrd_hwdec_name_valid("abcdefghijklmnopqrstuvwxyz01234"));   /* 31 */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(current_matches_build), cmocka_unit_test(linux_unchanged),
        cmocka_unit_test(macos_choices),         cmocka_unit_test(other_defaults),
        cmocka_unit_test(zero_copy),             cmocka_unit_test(name_valid),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
