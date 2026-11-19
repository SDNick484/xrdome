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
#elif defined(_WIN32)
    assert_int_equal(xrd_platform_current(), XRD_PLATFORM_WINDOWS);
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
    assert_string_equal(xrd_platform_hwdec_note(XRD_PLATFORM_LINUX), "");
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
    assert_string_equal(xrd_platform_hwdec_note(XRD_PLATFORM_MACOS), "");
}

static void windows_choices(void **state)
{
    (void)state;
    assert_string_equal(xrd_platform_name(XRD_PLATFORM_WINDOWS), "windows");
    /* WGL, not ANGLE: no zero-copy path, so a copy by default */
    assert_string_equal(xrd_platform_default_hwdec(XRD_PLATFORM_WINDOWS), "d3d11va-copy");
    assert_false(xrd_hwdec_is_zero_copy(xrd_platform_default_hwdec(XRD_PLATFORM_WINDOWS)));
    assert_true(xrd_hwdec_name_valid(xrd_platform_default_hwdec(XRD_PLATFORM_WINDOWS)));
    assert_false(xrd_platform_uses_x11(XRD_PLATFORM_WINDOWS));
    assert_false(xrd_platform_gl_forward_compat(XRD_PLATFORM_WINDOWS));
    assert_true(xrd_platform_force_fullscreen(XRD_PLATFORM_WINDOWS));
    assert_false(xrd_platform_has_xrdriver(XRD_PLATFORM_WINDOWS));
    assert_non_null(strstr(xrd_platform_hwdec_note(XRD_PLATFORM_WINDOWS), "copy by default"));
}

static void other_defaults(void **state)
{
    (void)state;
    assert_string_equal(xrd_platform_default_hwdec(XRD_PLATFORM_OTHER), "auto-safe");
    assert_string_equal(xrd_platform_name(XRD_PLATFORM_OTHER), "other");
    assert_false(xrd_platform_has_xrdriver(XRD_PLATFORM_OTHER));
    assert_string_equal(xrd_platform_hwdec_note(XRD_PLATFORM_OTHER), "");
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

static void path_absolute(void **state)
{
    (void)state;
    const xrd_platform L = XRD_PLATFORM_LINUX, W = XRD_PLATFORM_WINDOWS;
    assert_true(xrd_platform_path_absolute(L, "/x"));
    assert_false(xrd_platform_path_absolute(L, "x/y"));
    assert_false(xrd_platform_path_absolute(L, "C:\\x"));
    assert_false(xrd_platform_path_absolute(L, "\\x"));
    assert_false(xrd_platform_path_absolute(L, ""));
    assert_false(xrd_platform_path_absolute(L, NULL));
    assert_true(xrd_platform_path_absolute(XRD_PLATFORM_MACOS, "/Users/n"));
    assert_true(xrd_platform_path_absolute(W, "/x"));
    assert_true(xrd_platform_path_absolute(W, "C:\\Users\\n"));
    assert_true(xrd_platform_path_absolute(W, "d:/tmp"));
    assert_true(xrd_platform_path_absolute(W, "\\\\server\\share"));
    assert_false(xrd_platform_path_absolute(W, "C:rel")); /* relative to C:'s directory */
    assert_false(xrd_platform_path_absolute(W, "rel\\x"));
    assert_false(xrd_platform_path_absolute(W, ""));
}

static void user_path_posix(void **state)
{
    (void)state;
    char b[128];
    for (int i = 0; i < 3; i++) {
        xrd_platform p = i == 0 ? XRD_PLATFORM_LINUX : i == 1 ? XRD_PLATFORM_MACOS
                                                              : XRD_PLATFORM_OTHER;
        /* an absolute XDG_* wins */
        assert_int_equal(xrd_platform_user_path(p, XRD_USER_CONFIG, "/x", "/h", NULL, b, sizeof b), 0);
        assert_string_equal(b, "/x/xrdome/config.ini");
        assert_int_equal(xrd_platform_user_path(p, XRD_USER_STATE, "/s", "/h", NULL, b, sizeof b), 0);
        assert_string_equal(b, "/s/xrdome/files");
        /* else HOME; the known folder is never used */
        assert_int_equal(xrd_platform_user_path(p, XRD_USER_CONFIG, NULL, "/h", "C:\\k", b, sizeof b), 0);
        assert_string_equal(b, "/h/.config/xrdome/config.ini");
        assert_int_equal(xrd_platform_user_path(p, XRD_USER_STATE, "rel", "/h", NULL, b, sizeof b), 0);
        assert_string_equal(b, "/h/.local/state/xrdome/files");
        assert_int_equal(xrd_platform_user_path(p, XRD_USER_CONFIG, "", "/h", NULL, b, sizeof b), 0);
        assert_string_equal(b, "/h/.config/xrdome/config.ini");
        /* a Windows-style XDG_* is relative here */
        assert_int_equal(xrd_platform_user_path(p, XRD_USER_CONFIG, "C:\\x", "/h", NULL, b, sizeof b), 0);
        assert_string_equal(b, "/h/.config/xrdome/config.ini");
        /* nothing usable */
        assert_int_equal(xrd_platform_user_path(p, XRD_USER_CONFIG, NULL, NULL, "C:\\k", b, sizeof b), -1);
        assert_int_equal(xrd_platform_user_path(p, XRD_USER_STATE, "", "", NULL, b, sizeof b), -1);
    }
}

static void user_path_windows(void **state)
{
    (void)state;
    const xrd_platform W = XRD_PLATFORM_WINDOWS;
    char b[128];
    /* the known folders, HOME ignored */
    assert_int_equal(xrd_platform_user_path(W, XRD_USER_CONFIG, NULL, "/h",
                                            "C:\\Users\\n\\AppData\\Roaming", b, sizeof b), 0);
    assert_string_equal(b, "C:\\Users\\n\\AppData\\Roaming\\xrdome\\config.ini");
    assert_int_equal(xrd_platform_user_path(W, XRD_USER_STATE, "", "/h",
                                            "C:\\Users\\n\\AppData\\Local", b, sizeof b), 0);
    assert_string_equal(b, "C:\\Users\\n\\AppData\\Local\\xrdome\\files");
    /* an absolute XDG_* still wins (the tests set it) */
    assert_int_equal(xrd_platform_user_path(W, XRD_USER_CONFIG, "D:\\t", "/h", "C:\\k", b, sizeof b), 0);
    assert_string_equal(b, "D:\\t/xrdome/config.ini");
    assert_int_equal(xrd_platform_user_path(W, XRD_USER_STATE, "/tmp/s", NULL, "C:\\k", b, sizeof b), 0);
    assert_string_equal(b, "/tmp/s/xrdome/files");
    /* a relative one does not */
    assert_int_equal(xrd_platform_user_path(W, XRD_USER_STATE, "rel", NULL, "C:\\k", b, sizeof b), 0);
    assert_string_equal(b, "C:\\k\\xrdome\\files");
    /* no known folder: HOME is not a fallback */
    assert_int_equal(xrd_platform_user_path(W, XRD_USER_CONFIG, NULL, "/h", NULL, b, sizeof b), -1);
    assert_int_equal(xrd_platform_user_path(W, XRD_USER_CONFIG, NULL, "/h", "", b, sizeof b), -1);
}

static void user_path_fits(void **state)
{
    (void)state;
    /* strlen("/h/.config/xrdome/config.ini") == 28 */
    char exact[29], short1[28];
    assert_int_equal(xrd_platform_user_path(XRD_PLATFORM_LINUX, XRD_USER_CONFIG, NULL, "/h", NULL,
                                            exact, sizeof exact), 0);
    assert_string_equal(exact, "/h/.config/xrdome/config.ini");
    assert_int_equal(xrd_platform_user_path(XRD_PLATFORM_LINUX, XRD_USER_CONFIG, NULL, "/h", NULL,
                                            short1, sizeof short1), -1);
    /* strlen("C:\\k\\xrdome\\files") == 17 */
    char wexact[18], wshort[17];
    assert_int_equal(xrd_platform_user_path(XRD_PLATFORM_WINDOWS, XRD_USER_STATE, NULL, NULL,
                                            "C:\\k", wexact, sizeof wexact), 0);
    assert_int_equal(xrd_platform_user_path(XRD_PLATFORM_WINDOWS, XRD_USER_STATE, NULL, NULL,
                                            "C:\\k", wshort, sizeof wshort), -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(current_matches_build), cmocka_unit_test(linux_unchanged),
        cmocka_unit_test(macos_choices),         cmocka_unit_test(windows_choices),
        cmocka_unit_test(other_defaults),
        cmocka_unit_test(zero_copy),             cmocka_unit_test(name_valid),
        cmocka_unit_test(path_absolute),         cmocka_unit_test(user_path_posix),
        cmocka_unit_test(user_path_windows),     cmocka_unit_test(user_path_fits),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
