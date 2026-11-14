#define _POSIX_C_SOURCE 200809L /* mkdtemp, setenv, symlink */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

#include "driver/xrdriver.h"

#define NOW 1794669614LL

/* What the driver writes while a Viture Pro is connected in 1080p (its
 * src/state.c write_state), with the license line shortened. */
static const char CONNECTED[] =
    "heartbeat=1794669613\n"
    "hardware_id=74c5fcdd\n"
    "device_license={\"hardwareId\":\"74c5fcdd\",\"tiers\":{}}\n"
    "connected_device_brand=VITURE\n"
    "connected_device_model=Pro\n"
    "calibration_setup=AUTOMATIC\n"
    "calibration_state=CALIBRATED\n"
    "sbs_mode_supported=true\n"
    "sbs_mode_enabled=false\n"
    "connected_device_full_distance_cm=147.62\n"
    "firmware_update_recommended=false\n";

static void parse_connected(void **s)
{
    (void)s;
    xrd_xrdriver_state st;
    xrd_xrdriver_parse_state(CONNECTED, &st);
    assert_true(st.have_heartbeat);
    assert_int_equal(st.heartbeat, 1794669613LL);
    assert_true(st.have_sbs);
    assert_true(st.sbs_supported);
    assert_false(st.sbs_enabled);
    assert_true(xrd_xrdriver_fresh(&st, NOW));
}

static void parse_no_glasses(void **s)
{
    (void)s;
    /* The driver with no glasses: heartbeat and license only. */
    xrd_xrdriver_state st;
    xrd_xrdriver_parse_state("heartbeat=1794669614\nhardware_id=74c5\n"
                             "device_license={\"tiers\":{}}\n", &st);
    assert_true(st.have_heartbeat);
    assert_false(st.have_sbs);
}

static void parse_enabled_and_unsupported(void **s)
{
    (void)s;
    xrd_xrdriver_state st;
    xrd_xrdriver_parse_state("heartbeat=1\nsbs_mode_supported=true\nsbs_mode_enabled=true", &st);
    assert_true(st.have_sbs);
    assert_true(st.sbs_enabled);
    xrd_xrdriver_parse_state("heartbeat=1\nsbs_mode_supported=false\nsbs_mode_enabled=false\n",
                             &st);
    assert_true(st.have_sbs);
    assert_false(st.sbs_supported);
}

static void parse_bad_values(void **s)
{
    (void)s;
    xrd_xrdriver_state st;
    /* A value other than true or false does not count as the line. */
    xrd_xrdriver_parse_state("heartbeat=12x\nsbs_mode_supported=yes\nsbs_mode_enabled=false\n",
                             &st);
    assert_false(st.have_heartbeat);
    assert_false(st.have_sbs);
    xrd_xrdriver_parse_state("", &st);
    assert_false(st.have_heartbeat);
    assert_false(st.have_sbs);
    xrd_xrdriver_parse_state("heartbeat=\nsbs_mode_enabled\n=true\n", &st);
    assert_false(st.have_heartbeat);
    assert_false(st.have_sbs);
    /* A very long line is skipped without losing the lines after it. */
    char big[600];
    memset(big, 'x', sizeof big);
    memcpy(big, "device_license=", 15);
    big[sizeof big - 1] = '\0';
    char text[800];
    snprintf(text, sizeof text, "%s\nheartbeat=7\n", big);
    xrd_xrdriver_parse_state(text, &st);
    assert_true(st.have_heartbeat);
    assert_int_equal(st.heartbeat, 7);
}

static void fresh_window(void **s)
{
    (void)s;
    xrd_xrdriver_state st = {.have_heartbeat = true};
    st.heartbeat = NOW;
    assert_true(xrd_xrdriver_fresh(&st, NOW));
    st.heartbeat = NOW - XRD_XRDRIVER_FRESH_S;
    assert_true(xrd_xrdriver_fresh(&st, NOW));
    st.heartbeat = NOW - XRD_XRDRIVER_FRESH_S - 1; /* the driver stopped */
    assert_false(xrd_xrdriver_fresh(&st, NOW));
    st.heartbeat = NOW + XRD_XRDRIVER_FRESH_S + 1; /* clock stepped back */
    assert_false(xrd_xrdriver_fresh(&st, NOW));
    st.have_heartbeat = false;
    st.heartbeat = NOW;
    assert_false(xrd_xrdriver_fresh(&st, NOW));
}

static void request_text(void **s)
{
    (void)s;
    assert_string_equal(xrd_xrdriver_sbs_request(true), "sbs_mode=enable\n");
    assert_string_equal(xrd_xrdriver_sbs_request(false), "sbs_mode=disable\n");
}

static void dir_and_paths(void **s)
{
    (void)s;
    char buf[64];
    unsetenv(XRD_XRDRIVER_DIR_ENV);
    assert_string_equal(xrd_xrdriver_dir(), "/dev/shm");
    assert_int_equal(xrd_xrdriver_path(XRD_XRDRIVER_CONTROL, buf, sizeof buf), 0);
    assert_string_equal(buf, "/dev/shm/xr_driver_control");
    assert_int_equal(xrd_xrdriver_path(XRD_XRDRIVER_STATE, buf, sizeof buf), 0);
    assert_string_equal(buf, "/dev/shm/xr_driver_state");
    setenv(XRD_XRDRIVER_DIR_ENV, "", 1);
    assert_string_equal(xrd_xrdriver_dir(), "/dev/shm");
    setenv(XRD_XRDRIVER_DIR_ENV, "/tmp/fake", 1);
    assert_int_equal(xrd_xrdriver_path(XRD_XRDRIVER_CONTROL, buf, sizeof buf), 0);
    assert_string_equal(buf, "/tmp/fake/xr_driver_control");
    char tiny[10];
    assert_int_equal(xrd_xrdriver_path(XRD_XRDRIVER_CONTROL, tiny, sizeof tiny), -1);
    unsetenv(XRD_XRDRIVER_DIR_ENV);
}

static void slurp(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");
    assert_non_null(f);
    size_t n = fread(buf, 1, len - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert_non_null(f);
    fputs(text, f);
    fclose(f);
}

static void request_writes_file(void **s)
{
    (void)s;
    char dir[] = "/tmp/xrd_drv_XXXXXX", path[128], got[64];
    assert_non_null(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/xr_driver_control", dir);

    assert_int_equal(xrd_xrdriver_request(path, xrd_xrdriver_sbs_request(true)), 0);
    slurp(path, got, sizeof got);
    assert_string_equal(got, "sbs_mode=enable\n");
    /* An unread request is replaced, not appended to. */
    write_file(path, "recenter_screen=true\nrecalibrate=true\n");
    assert_int_equal(xrd_xrdriver_request(path, xrd_xrdriver_sbs_request(false)), 0);
    slurp(path, got, sizeof got);
    assert_string_equal(got, "sbs_mode=disable\n");
    unlink(path);

    /* Never through a symlink: the target is left alone. */
    char target[128];
    snprintf(target, sizeof target, "%s/target", dir);
    write_file(target, "keep\n");
    assert_int_equal(symlink(target, path), 0);
    errno = 0;
    assert_int_equal(xrd_xrdriver_request(path, xrd_xrdriver_sbs_request(true)), -1);
    assert_int_equal(errno, ELOOP);
    slurp(target, got, sizeof got);
    assert_string_equal(got, "keep\n");
    unlink(path);
    unlink(target);

    /* No such directory (no driver ever ran). */
    char missing[160];
    snprintf(missing, sizeof missing, "%s/nodir/xr_driver_control", dir);
    errno = 0;
    assert_int_equal(xrd_xrdriver_request(missing, "x\n"), -1);
    assert_int_equal(errno, ENOENT);
    rmdir(dir);
}

static void read_state_file(void **s)
{
    (void)s;
    char dir[] = "/tmp/xrd_drv_XXXXXX", path[128];
    assert_non_null(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/xr_driver_state", dir);
    xrd_xrdriver_state st;

    errno = 0;
    assert_int_equal(xrd_xrdriver_read_state(path, &st), -1); /* missing */
    assert_int_equal(errno, ENOENT);

    write_file(path, CONNECTED);
    assert_int_equal(xrd_xrdriver_read_state(path, &st), 0);
    assert_true(st.have_heartbeat && st.have_sbs && st.sbs_supported && !st.sbs_enabled);

    write_file(path, ""); /* empty after the retries: read, but nothing in it */
    assert_int_equal(xrd_xrdriver_read_state(path, &st), 0);
    assert_false(st.have_heartbeat);
    unlink(path);
    rmdir(dir);
}

int main(void)
{
    const struct CMUnitTest t[] = {
        cmocka_unit_test(parse_connected),
        cmocka_unit_test(parse_no_glasses),
        cmocka_unit_test(parse_enabled_and_unsupported),
        cmocka_unit_test(parse_bad_values),
        cmocka_unit_test(fresh_window),
        cmocka_unit_test(request_text),
        cmocka_unit_test(dir_and_paths),
        cmocka_unit_test(request_writes_file),
        cmocka_unit_test(read_state_file),
    };
    return cmocka_run_group_tests(t, NULL, NULL);
}
