#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "driver/xrdriver.h"
#include "test_compat.h"
#include "xrdriver_check.h"

static char dir[64];

static int setup(void **state)
{
    (void)state;
    snprintf(dir, sizeof dir, "/tmp/xrd-viture-drv-XXXXXX");
    return mkdtemp(dir) ? 0 : -1;
}

static int teardown(void **state)
{
    (void)state;
    xrd_test_rmtree(dir);
    return 0;
}

static void write_state(const char *text)
{
    char path[128];
    snprintf(path, sizeof path, "%s/xr_driver_state", dir);
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    fputs(text, f);
    fclose(f);
}

static void fresh_heartbeat_means_running(void **state)
{
    (void)state;
    write_state("device_brand=Viture\nheartbeat=1000\nsbs_mode_supported=true\n");
    assert_true(viture_xrdriver_running(dir, 1000));
    assert_true(viture_xrdriver_running(dir, 1005));
    assert_true(viture_xrdriver_running(dir, 995)); /* the clock may be behind the driver's */
    assert_false(viture_xrdriver_running(dir, 1006));
    assert_false(viture_xrdriver_running(dir, 994));
}

static void no_state_means_not_running(void **state)
{
    (void)state;
    assert_false(viture_xrdriver_running("/nonexistent/dir", 1000));
    write_state("");
    assert_false(viture_xrdriver_running(dir, 1000));
    write_state("sbs_mode_enabled=true\n");
    assert_false(viture_xrdriver_running(dir, 1000));
    write_state("heartbeat=soon\n");
    assert_false(viture_xrdriver_running(dir, 1000));
}

static void long_lines_do_not_hide_the_heartbeat(void **state)
{
    (void)state;
    /* A license line far longer than the reader's buffer, with text that
     * would be a heartbeat line if a piece of it started one (the reader's
     * pieces start at line offsets 0, 255, 510), then the real heartbeat. */
    char big[2000];
    memset(big, 'b', sizeof big);
    memcpy(big + (510 - strlen("device_license=")), "heartbeat=1", 11);
    big[sizeof big - 1] = '\0';
    char text[2300];
    snprintf(text, sizeof text, "device_license=%s\nheartbeat=500\n", big);
    write_state(text);
    assert_true(viture_xrdriver_running(dir, 500));
    assert_false(viture_xrdriver_running(dir, 1));
}

/* Agreement with xrdome's own reader of the same file. */
static void agrees_with_xrdome(void **state)
{
    (void)state;
    static const char *const texts[] = {
        "heartbeat=1000\n",
        "heartbeat=1000\r\n",
        "foo=bar\nheartbeat=998\n",
        "heartbeat=1000abc\n",
        "heartbeat=\n",
        "",
    };
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        xrd_xrdriver_state st;
        xrd_xrdriver_parse_state(texts[i], &st);
        write_state(texts[i]);
        for (long long now = 990; now <= 1010; now++) {
            bool want = st.have_heartbeat && xrd_xrdriver_fresh(&st, now);
            assert_true(viture_xrdriver_running(dir, now) == want);
        }
    }
}

static void directory_follows_the_environment(void **state)
{
    (void)state;
    unsetenv("XRDOME_XR_DRIVER_DIR");
    assert_string_equal(viture_xrdriver_dir(), "/dev/shm");
    setenv("XRDOME_XR_DRIVER_DIR", dir, 1);
    assert_string_equal(viture_xrdriver_dir(), dir);
    assert_string_equal(viture_xrdriver_dir(), xrd_xrdriver_dir());
    setenv("XRDOME_XR_DRIVER_DIR", "", 1);
    assert_string_equal(viture_xrdriver_dir(), "/dev/shm");
    unsetenv("XRDOME_XR_DRIVER_DIR");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(fresh_heartbeat_means_running, setup, teardown),
        cmocka_unit_test_setup_teardown(no_state_means_not_running, setup, teardown),
        cmocka_unit_test_setup_teardown(long_lines_do_not_hide_the_heartbeat, setup, teardown),
        cmocka_unit_test_setup_teardown(agrees_with_xrdome, setup, teardown),
        cmocka_unit_test_setup_teardown(directory_follows_the_environment, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
