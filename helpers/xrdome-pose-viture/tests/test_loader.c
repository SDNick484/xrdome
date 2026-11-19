#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "viture_loader.h"

static void loads_every_entry_point(void **state)
{
    (void)state;
    viture_api api;
    char err[300] = "";
    assert_int_equal(viture_load(XRD_FAKE_SDK, &api, err, sizeof err), 0);
    assert_non_null(api.lib);
    assert_non_null(api.create);
    assert_non_null(api.initialize);
    assert_non_null(api.start);
    assert_non_null(api.stop);
    assert_non_null(api.shutdown);
    assert_non_null(api.destroy);
    assert_non_null(api.get_device_type);
    assert_non_null(api.is_product_id_valid);
    assert_non_null(api.is_product_support_imu_frequency);
    assert_non_null(api.register_imu_pose_callback);
    assert_non_null(api.open_imu);
    assert_non_null(api.close_imu);
    assert_non_null(api.get_display_mode);
    assert_non_null(api.set_display_mode);

    /* The pointers work. */
    assert_int_equal(api.is_product_id_valid(0x1019), 1);
    assert_int_equal(api.is_product_id_valid(0x1234), 0);
    viture_unload(&api);
    assert_null(api.lib);
}

static void missing_library_is_named(void **state)
{
    (void)state;
    viture_api api;
    char err[300] = "";
    assert_int_equal(viture_load("/nonexistent/dir/libglasses.so", &api, err, sizeof err), -1);
    assert_non_null(strstr(err, "/nonexistent/dir/libglasses.so"));
    assert_non_null(strstr(err, "cannot load"));
    assert_null(api.lib);
}

static void not_a_library_fails(void **state)
{
    (void)state;
    viture_api api;
    char err[300] = "";
    /* A text file: the loader must say so, not crash. */
    assert_int_equal(viture_load(XRD_FAKE_SDK_SOURCE, &api, err, sizeof err), -1);
    assert_non_null(strstr(err, "cannot load"));
}

static void missing_symbol_is_named(void **state)
{
    (void)state;
    viture_api api;
    char err[300] = "";
    assert_int_equal(viture_load(XRD_FAKE_SDK_NOSTART, &api, err, sizeof err), -1);
    assert_non_null(strstr(err, "xr_device_provider_start"));
    assert_non_null(strstr(err, "fake_viture_nostart"));
    assert_null(api.lib);
    assert_null(api.create);
}

static void default_name_is_the_platforms(void **state)
{
    (void)state;
    const char *n = viture_default_lib();
#if defined(_WIN32)
    assert_string_equal(n, "libglasses.dll");
#elif defined(__APPLE__)
    assert_string_equal(n, "libglasses.dylib");
#else
    assert_string_equal(n, "libglasses.so");
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(loads_every_entry_point),
        cmocka_unit_test(missing_library_is_named),
        cmocka_unit_test(not_a_library_fails),
        cmocka_unit_test(missing_symbol_is_named),
        cmocka_unit_test(default_name_is_the_platforms),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
