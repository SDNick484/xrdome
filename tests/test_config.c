#define _POSIX_C_SOURCE 200809L /* mkstemp */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

#include "config.h"

#define TOL 1e-4

static const char SPEC_INI[] =
    "# xrdome config\n"
    "[general]\n"
    "pose = udp-opentrack:port=4242\n"
    "output = auto\n"
    "\n"
    "[device.viture-pro]\n"
    "fov_diag = 43.0\n"
    "zoom = 1.0\n"
    "\n"
    "[device.xreal-air2pro]\n"
    "fov_diag = 45.0\n"
    "zoom = 1.0\n";

/* Writes text to a fresh temp file; path must hold 64 bytes. */
static void write_tmp(char *path, const char *text)
{
    strcpy(path, "/tmp/xrd_cfg_XXXXXX");
    int fd = mkstemp(path);
    assert_true(fd >= 0);
    assert_int_equal((int)write(fd, text, strlen(text)), (int)strlen(text));
    close(fd);
}

static void defaults_match_spec(void **state)
{
    (void)state;
    xrd_config c;
    memset(&c, 0xAA, sizeof c);
    xrd_config_defaults(&c);
    assert_string_equal(c.file, "");
    assert_string_equal(c.pose, "udp-opentrack:port=4242");
    assert_string_equal(c.output, "auto");
    assert_string_equal(c.device, "viture-pro");
    assert_float_equal(c.fov_diag, 43.0f, TOL);
    assert_float_equal(c.zoom, 1.0f, TOL);
    assert_int_equal(c.layout, XRD_LAYOUT_SBS_LR);
    assert_false(c.require_hwdec);
    assert_false(c.windowed);
    assert_string_equal(c.timing_log, "");
}

static void ini_absent_is_ok(void **state)
{
    (void)state;
    xrd_config c;
    char err[256] = "";
    xrd_config_defaults(&c);
    assert_int_equal(xrd_config_load_ini(&c, "/nonexistent/xrdome.ini", err, sizeof err), 0);
    assert_float_equal(c.fov_diag, 43.0f, TOL);
}

static void ini_spec_xreal_via_load(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, SPEC_INI);
    char *argv[] = {"xrdome", "--device", "xreal-air2pro", "movie.mp4", NULL};
    xrd_config c;
    int rc = xrd_config_load(&c, path, 4, argv, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, 0);
    assert_float_equal(c.fov_diag, 45.0f, TOL);
    assert_string_equal(c.device, "xreal-air2pro");
    assert_string_equal(c.pose, "udp-opentrack:port=4242");
    assert_string_equal(c.file, "movie.mp4");
}

static void ini_reads_only_selected_device_section(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, "[device.viture-pro]\nfov_diag = 40\n[device.xreal-air2pro]\nfov_diag = 50\n");
    xrd_config c;
    xrd_config_defaults(&c);
    int rc = xrd_config_load_ini(&c, path, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, 0);
    assert_float_equal(c.fov_diag, 40.0f, TOL);
}

static void ini_comments_and_blank_lines(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, "# top\n\n  [general]  \n  # c\n  output=HDMI-1  \n\tpose\t=\tudp-opentrack:port=5000\n");
    xrd_config c;
    xrd_config_defaults(&c);
    int rc = xrd_config_load_ini(&c, path, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, 0);
    assert_string_equal(c.output, "HDMI-1");
    assert_string_equal(c.pose, "udp-opentrack:port=5000");
}

static void ini_zoom_out_of_range(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, "[device.viture-pro]\nzoom = 9\n");
    xrd_config c;
    xrd_config_defaults(&c);
    int rc = xrd_config_load_ini(&c, path, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, -1);
    assert_non_null(strstr(err, "zoom"));
}

static void ini_fov_out_of_range(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, "[device.viture-pro]\nfov_diag = 5\n");
    xrd_config c;
    xrd_config_defaults(&c);
    int rc = xrd_config_load_ini(&c, path, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, -1);
    assert_non_null(strstr(err, "fov_diag"));
}

static void ini_non_numeric_and_unknown_key(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    xrd_config c;

    write_tmp(path, "[device.viture-pro]\nzoom = fast\n");
    xrd_config_defaults(&c);
    int rc = xrd_config_load_ini(&c, path, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, -1);
    assert_non_null(strstr(err, "zoom"));

    write_tmp(path, "[general]\nbogus = 1\n");
    xrd_config_defaults(&c);
    rc = xrd_config_load_ini(&c, path, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, -1);
    assert_non_null(strstr(err, "bogus"));
}

static void args_layout_mono(void **state)
{
    (void)state;
    char err[256] = "";
    char *argv[] = {"xrdome", "--layout", "mono", "a.mp4", NULL};
    xrd_config c;
    xrd_config_defaults(&c);
    assert_int_equal(xrd_config_parse_args(&c, 4, argv, err, sizeof err), 0);
    assert_int_equal(c.layout, XRD_LAYOUT_MONO);
    char *argv2[] = {"xrdome", "--layout", "sbs-rl", "a.mp4", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 4, argv2, err, sizeof err), 0);
    assert_int_equal(c.layout, XRD_LAYOUT_SBS_RL);
}

static void args_cli_overrides_ini(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, SPEC_INI);
    char *argv[] = {"xrdome", "--zoom", "2.5", "--pose", "udp-opentrack:port=9999",
                    "--output", "DP-2", "--layout", "mono", "a.mp4", NULL};
    xrd_config c;
    int rc = xrd_config_load(&c, path, 10, argv, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, 0);
    assert_float_equal(c.zoom, 2.5f, TOL);
    assert_string_equal(c.pose, "udp-opentrack:port=9999");
    assert_string_equal(c.output, "DP-2");
    assert_int_equal(c.layout, XRD_LAYOUT_MONO);
    assert_float_equal(c.fov_diag, 43.0f, TOL);
}

static void args_bad_layout(void **state)
{
    (void)state;
    char err[256] = "";
    char *argv[] = {"xrdome", "--layout", "tb", "a.mp4", NULL};
    xrd_config c;
    xrd_config_defaults(&c);
    assert_int_equal(xrd_config_parse_args(&c, 4, argv, err, sizeof err), -1);
    assert_non_null(strstr(err, "tb"));
}

static void args_bad_proj(void **state)
{
    (void)state;
    char err[256] = "";
    char *argv[] = {"xrdome", "--proj", "fisheye", "a.mp4", NULL};
    xrd_config c;
    xrd_config_defaults(&c);
    assert_int_equal(xrd_config_parse_args(&c, 4, argv, err, sizeof err), -1);
    assert_non_null(strstr(err, "hequirect"));

    char *ok[] = {"xrdome", "--proj", "hequirect", "a.mp4", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 4, ok, err, sizeof err), 0);
}

static void args_missing_file(void **state)
{
    (void)state;
    char err[256] = "";
    char *argv[] = {"xrdome", "--windowed", NULL};
    xrd_config c;
    xrd_config_defaults(&c);
    assert_int_equal(xrd_config_parse_args(&c, 2, argv, err, sizeof err), -1);
    assert_true(strlen(err) > 0);
}

static void args_extra_positional(void **state)
{
    (void)state;
    char err[256] = "";
    char *argv[] = {"xrdome", "a.mp4", "b.mp4", NULL};
    xrd_config c;
    xrd_config_defaults(&c);
    assert_int_equal(xrd_config_parse_args(&c, 3, argv, err, sizeof err), -1);
    assert_non_null(strstr(err, "b.mp4"));
}

static void args_path_with_spaces(void **state)
{
    (void)state;
    char err[256] = "";
    char *argv[] = {"xrdome", "--timing-log", "/tmp/my logs/t.csv", "/home/me/My Videos/clip 180.mp4", NULL};
    xrd_config c;
    xrd_config_defaults(&c);
    assert_int_equal(xrd_config_parse_args(&c, 4, argv, err, sizeof err), 0);
    assert_string_equal(c.file, "/home/me/My Videos/clip 180.mp4");
    assert_string_equal(c.timing_log, "/tmp/my logs/t.csv");
}

static void args_flags_and_repeat_calls(void **state)
{
    (void)state;
    char err[256] = "";
    xrd_config c;
    xrd_config_defaults(&c);
    char *a1[] = {"xrdome", "a.mp4", "--require-hwdec", "--windowed", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 4, a1, err, sizeof err), 0);
    assert_true(c.require_hwdec);
    assert_true(c.windowed);
    assert_string_equal(c.file, "a.mp4");
    /* a second call in the same process must start from a clean getopt state */
    char *a2[] = {"xrdome", "--zoom", "1.5", "b.mp4", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 4, a2, err, sizeof err), 0);
    assert_float_equal(c.zoom, 1.5f, TOL);
    assert_string_equal(c.file, "b.mp4");
}

static void args_bad_values_and_unknown_option(void **state)
{
    (void)state;
    char err[256] = "";
    xrd_config c;
    xrd_config_defaults(&c);

    char *z[] = {"xrdome", "--zoom", "0.1", "a.mp4", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 4, z, err, sizeof err), -1);
    assert_non_null(strstr(err, "zoom"));

    char *zn[] = {"xrdome", "--zoom", "abc", "a.mp4", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 4, zn, err, sizeof err), -1);
    assert_non_null(strstr(err, "zoom"));

    char *u[] = {"xrdome", "--frobnicate", "a.mp4", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 3, u, err, sizeof err), -1);
    assert_non_null(strstr(err, "frobnicate"));

    char *m[] = {"xrdome", "a.mp4", "--pose", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 3, m, err, sizeof err), -1);
    assert_non_null(strstr(err, "pose"));

    char *long_pose[] = {"xrdome", "--pose", "udp-opentrack:port=4242,"
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        "a.mp4", NULL};
    assert_int_equal(xrd_config_parse_args(&c, 4, long_pose, err, sizeof err), -1);
    assert_non_null(strstr(err, "pose"));
}

static void load_unknown_device_names_it(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, SPEC_INI);
    char *argv[] = {"xrdome", "--device", "rokid-max", "a.mp4", NULL};
    xrd_config c;
    int rc = xrd_config_load(&c, path, 4, argv, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, -1);
    assert_non_null(strstr(err, "rokid-max"));

    rc = xrd_config_load(&c, NULL, 4, argv, err, sizeof err);
    assert_int_equal(rc, -1);
    assert_non_null(strstr(err, "rokid-max"));
}

static void load_ini_defined_device_ok(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, "[device.rokid-max]\nfov_diag = 50\n");
    char *argv[] = {"xrdome", "--device", "rokid-max", "a.mp4", NULL};
    xrd_config c;
    int rc = xrd_config_load(&c, path, 4, argv, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, 0);
    assert_float_equal(c.fov_diag, 50.0f, TOL);
}

static void load_null_ini_and_builtin_fov(void **state)
{
    (void)state;
    char err[256] = "";
    xrd_config c;
    char *a[] = {"xrdome", "--device", "xreal-air2pro", "a.mp4", NULL};
    assert_int_equal(xrd_config_load(&c, NULL, 4, a, err, sizeof err), 0);
    assert_float_equal(c.fov_diag, 45.0f, TOL);
    char *b[] = {"xrdome", "a.mp4", NULL};
    assert_int_equal(xrd_config_load(&c, NULL, 2, b, err, sizeof err), 0);
    assert_string_equal(c.device, "viture-pro");
    assert_float_equal(c.fov_diag, 43.0f, TOL);
}

static void load_bad_ini_reports_key(void **state)
{
    (void)state;
    char path[64], err[256] = "";
    write_tmp(path, "[device.viture-pro]\nzoom = 9\n");
    char *argv[] = {"xrdome", "a.mp4", NULL};
    xrd_config c;
    int rc = xrd_config_load(&c, path, 2, argv, err, sizeof err);
    unlink(path);
    assert_int_equal(rc, -1);
    assert_non_null(strstr(err, "zoom"));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(defaults_match_spec),
        cmocka_unit_test(ini_absent_is_ok),
        cmocka_unit_test(ini_spec_xreal_via_load),
        cmocka_unit_test(ini_reads_only_selected_device_section),
        cmocka_unit_test(ini_comments_and_blank_lines),
        cmocka_unit_test(ini_zoom_out_of_range),
        cmocka_unit_test(ini_fov_out_of_range),
        cmocka_unit_test(ini_non_numeric_and_unknown_key),
        cmocka_unit_test(args_layout_mono),
        cmocka_unit_test(args_cli_overrides_ini),
        cmocka_unit_test(args_bad_layout),
        cmocka_unit_test(args_bad_proj),
        cmocka_unit_test(args_missing_file),
        cmocka_unit_test(args_extra_positional),
        cmocka_unit_test(args_path_with_spaces),
        cmocka_unit_test(args_flags_and_repeat_calls),
        cmocka_unit_test(args_bad_values_and_unknown_option),
        cmocka_unit_test(load_unknown_device_names_it),
        cmocka_unit_test(load_ini_defined_device_ok),
        cmocka_unit_test(load_null_ini_and_builtin_fov),
        cmocka_unit_test(load_bad_ini_reports_key),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
