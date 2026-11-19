#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmocka.h>

#include "test_compat.h"

#include "platform/exepath.h"

/* The directory the pick tests pretend holds ffprobe. */
static bool exec_in_bundle(const char *path) { return !strcmp(path, "/opt/xrdome/ffprobe"); }
static bool exec_win_bundle(const char *path) { return !strcmp(path, "C:\\xrdome\\ffprobe.exe"); }
static bool exec_nowhere(const char *path) { (void)path; return false; }

static void dirname_cases(void **state)
{
    (void)state;
    char b[64];
    assert_true(xrd_exepath_dirname(XRD_PLATFORM_LINUX, "/a/b/xrdome", b, sizeof b));
    assert_string_equal(b, "/a/b");
    assert_true(xrd_exepath_dirname(XRD_PLATFORM_LINUX, "/xrdome", b, sizeof b));
    assert_string_equal(b, "/");
    assert_false(xrd_exepath_dirname(XRD_PLATFORM_LINUX, "xrdome", b, sizeof b));
    assert_string_equal(b, "");
    assert_false(xrd_exepath_dirname(XRD_PLATFORM_LINUX, "/a/b/xrdome", b, 4)); /* "/a/b" needs 5 */
    assert_false(xrd_exepath_dirname(XRD_PLATFORM_LINUX, NULL, b, sizeof b));
}

static void windows_dirname(void **state)
{
    (void)state;
    char b[64];
    assert_true(xrd_exepath_dirname(XRD_PLATFORM_WINDOWS, "C:\\Users\\me\\xrdome\\xrdome.exe", b, sizeof b));
    assert_string_equal(b, "C:\\Users\\me\\xrdome");
    assert_true(xrd_exepath_dirname(XRD_PLATFORM_WINDOWS, "C:/x/xrdome.exe", b, sizeof b)); /* either slash */
    assert_string_equal(b, "C:/x");
    assert_true(xrd_exepath_dirname(XRD_PLATFORM_WINDOWS, "C:\\xrdome.exe", b, sizeof b));
    assert_string_equal(b, "C:\\"); /* the drive root keeps its slash */
    assert_true(xrd_exepath_dirname(XRD_PLATFORM_WINDOWS, "\\\\srv\\share\\x.exe", b, sizeof b));
    assert_string_equal(b, "\\\\srv\\share");
    assert_false(xrd_exepath_dirname(XRD_PLATFORM_WINDOWS, "xrdome.exe", b, sizeof b));
    assert_false(xrd_exepath_dirname(XRD_PLATFORM_WINDOWS, "C:\\a\\b.exe", b, 4)); /* "C:\\a" needs 5 */
    /* A backslash is a name character elsewhere. */
    assert_false(xrd_exepath_dirname(XRD_PLATFORM_LINUX, "C:\\xrdome.exe", b, sizeof b));
}

static void windows_pick(void **state)
{
    (void)state;
    char o[64];
    const xrd_platform w = XRD_PLATFORM_WINDOWS;
    /* Beside the executable: ffprobe.exe, joined with a backslash; PATH is ';' separated. */
    assert_true(xrd_exepath_pick_ffprobe(w, NULL, "C:\\xrdome", "C:\\Windows;C:\\bin", exec_win_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "C:\\xrdome\\ffprobe.exe");
    /* No ffprobe.exe there, or no way to know: PATH (CreateProcessW adds .exe). */
    assert_true(xrd_exepath_pick_ffprobe(w, NULL, "C:\\other", "C:\\bin", exec_win_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(w, NULL, "", "C:\\bin", exec_win_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    /* The configured value wins, whatever it is. */
    assert_true(xrd_exepath_pick_ffprobe(w, "D:\\tools\\ffprobe.exe", "C:\\xrdome", NULL, exec_win_bundle,
                                         o, sizeof o));
    assert_string_equal(o, "D:\\tools\\ffprobe.exe");
    /* Unix names do not match: the Windows bundle's program is ffprobe.exe. */
    assert_true(xrd_exepath_pick_ffprobe(w, NULL, "/opt/xrdome", NULL, exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    /* The drive root: no doubled slash. */
    assert_true(xrd_exepath_pick_ffprobe(w, NULL, "C:\\", NULL, exec_nowhere, o, sizeof o));
    assert_string_equal(o, "ffprobe");
}

static void windows_directory_on_path(void **state)
{
    (void)state;
    char o[64];
    const xrd_platform w = XRD_PLATFORM_WINDOWS;
    /* On PATH under any case, either slash, a trailing slash, or quotes: PATH's order decides. */
    const char *on_path[] = {"C:\\Windows;C:\\xrdome", "c:\\XRDOME", "C:/xrdome", "C:\\xrdome\\;C:\\x",
                             "\"C:\\xrdome\";C:\\x"};
    for (size_t i = 0; i < sizeof on_path / sizeof *on_path; i++) {
        assert_true(xrd_exepath_pick_ffprobe(w, NULL, "C:\\xrdome", on_path[i], exec_win_bundle, o, sizeof o));
        assert_string_equal(o, "ffprobe");
    }
    /* ':' does not separate on Windows (a drive letter), and a longer name is not the entry. */
    assert_true(xrd_exepath_pick_ffprobe(w, NULL, "C:\\xrdome", "C:\\xrdome2;D:\\a", exec_win_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "C:\\xrdome\\ffprobe.exe");
    /* ';' does not separate elsewhere. */
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/opt/xrdome", "/opt/xrdome;/bin",
                                         exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "/opt/xrdome/ffprobe");
}

static void configured_wins(void **state)
{
    (void)state;
    char o[64];
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, "/usr/bin/ffprobe", "/opt/xrdome", "/usr/bin", exec_in_bundle,
                                         o, sizeof o));
    assert_string_equal(o, "/usr/bin/ffprobe");
    /* An explicit "ffprobe" is a request for PATH, not for the bundle. */
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, "ffprobe", "/opt/xrdome", "/usr/bin", exec_in_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "ffprobe");
}

static void beside_the_executable(void **state)
{
    (void)state;
    char o[64];
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/opt/xrdome", "/usr/bin:/bin", exec_in_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "/opt/xrdome/ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, "", "/opt/xrdome", NULL, exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "/opt/xrdome/ffprobe");
}

static void falls_back_to_path(void **state)
{
    (void)state;
    char o[64];
    /* No ffprobe beside it, directory unknown, no way to test. */
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/opt/xrdome", "/usr/bin", exec_nowhere, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "", "/usr/bin", exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, NULL, NULL, exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/opt/xrdome", "/usr/bin", NULL, o, sizeof o));
    assert_string_equal(o, "ffprobe");
}

static void directory_on_path_keeps_path_order(void **state)
{
    (void)state;
    char o[64];
    /* An install in a PATH directory: PATH's own order decides, as before. */
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/opt/xrdome", "/usr/bin:/opt/xrdome", exec_in_bundle,
                                         o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/opt/xrdome", "/opt/xrdome/:/bin", exec_in_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "ffprobe");
    /* A prefix of an entry is not the entry. */
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/opt/xrdome", "/opt/xrdome2:/opt", exec_in_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "/opt/xrdome/ffprobe");
}

static void root_directory(void **state)
{
    (void)state;
    char o[64];
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/", NULL, exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe"); /* "/ffprobe" is not the stub's path */
}

static void too_small_output(void **state)
{
    (void)state;
    char o[8];
    assert_false(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, "/opt/xrdome", "/bin", exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "");
    assert_false(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, "/a/long/configured/path", NULL, NULL, NULL, o, sizeof o));
    assert_string_equal(o, "");
}

static void is_exec_checks_the_file(void **state)
{
    (void)state;
    char dir[] = "/tmp/xrd_exepath_XXXXXX";
    assert_non_null(mkdtemp(dir));
    char f[128], x[128];
    snprintf(f, sizeof f, "%s/plain", dir);
    snprintf(x, sizeof x, "%s/prog", dir);
    FILE *fp = fopen(f, "w");
    assert_non_null(fp);
    fclose(fp);
    fp = fopen(x, "w");
    assert_non_null(fp);
    fclose(fp);
    assert_int_equal(chmod(f, 0644), 0);
    assert_int_equal(chmod(x, 0755), 0);
#ifndef _WIN32 /* Windows has no execute bit: any regular file is a program there */
    assert_false(xrd_exepath_is_exec(f));
#endif
    assert_true(xrd_exepath_is_exec(x));
    assert_false(xrd_exepath_is_exec(dir)); /* a directory is searchable, not a program */
    assert_false(xrd_exepath_is_exec("/nonexistent/ffprobe"));

    /* The real pick, with the real check, finds a program beside "the executable". */
    char o[256], p[128];
    snprintf(p, sizeof p, "%s/ffprobe", dir);
    assert_int_equal(rename(x, p), 0);
    assert_true(xrd_exepath_pick_ffprobe(XRD_PLATFORM_LINUX, NULL, dir, "/usr/bin", xrd_exepath_is_exec, o, sizeof o));
    assert_string_equal(o, p);
    unlink(p);
    unlink(f);
    rmdir(dir);
}

static void own_directory(void **state)
{
    (void)state;
    char d[4096];
#if defined(__APPLE__) || defined(__linux__)
    assert_true(xrd_exepath_dir(d, sizeof d));
    assert_true(d[0] == '/');
    assert_true(strlen(d) == 1 || d[strlen(d) - 1] != '/');
    char probe[4200];
    snprintf(probe, sizeof probe, "%s/test_exepath", d);
    assert_true(xrd_exepath_is_exec(probe)); /* this test binary lives there */
#elif defined(_WIN32)
    assert_true(xrd_exepath_dir(d, sizeof d));
    assert_true(d[0] != '\0' && d[strlen(d) - 1] != '\\');
    char probe[4200];
    snprintf(probe, sizeof probe, "%s\\test_exepath.exe", d);
    assert_true(xrd_exepath_is_exec(probe)); /* this test program lives there */
#else
    assert_false(xrd_exepath_dir(d, sizeof d));
    assert_string_equal(d, "");
#endif
    char tiny[2]; /* too small for any real directory: false, and empty */
    assert_false(xrd_exepath_dir(tiny, sizeof tiny));
    assert_string_equal(tiny, "");
}

static void default_is_the_bare_name_here(void **state)
{
    (void)state;
    /* Nothing next to the build's test binaries is called ffprobe. */
    char o[256];
    xrd_exepath_default_ffprobe(o, sizeof o);
    assert_string_equal(o, "ffprobe");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(dirname_cases),
        cmocka_unit_test(windows_dirname),
        cmocka_unit_test(windows_pick),
        cmocka_unit_test(windows_directory_on_path),
        cmocka_unit_test(configured_wins),
        cmocka_unit_test(beside_the_executable),
        cmocka_unit_test(falls_back_to_path),
        cmocka_unit_test(directory_on_path_keeps_path_order),
        cmocka_unit_test(root_directory),
        cmocka_unit_test(too_small_output),
        cmocka_unit_test(is_exec_checks_the_file),
        cmocka_unit_test(own_directory),
        cmocka_unit_test(default_is_the_bare_name_here),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
