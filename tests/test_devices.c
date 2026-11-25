#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <cmocka.h>

#include "devices.h"

static void builtin_viture(void **state)
{
    (void)state;
    float f = 0.f;
    assert_true(xrd_device_builtin("viture-pro", &f));
    assert_true(f == 43.0f);
}

static void builtin_xreal(void **state)
{
    (void)state;
    float f = 0.f;
    assert_true(xrd_device_builtin("xreal-air2pro", &f));
    assert_true(f == 45.0f);
}

static void unknown_false(void **state)
{
    (void)state;
    float f = 7.5f;
    assert_false(xrd_device_builtin("foo", &f));
    assert_true(f == 7.5f); /* untouched */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(builtin_viture),
        cmocka_unit_test(builtin_xreal),
        cmocka_unit_test(unknown_false),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
