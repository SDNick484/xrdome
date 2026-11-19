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

#include "platform/exepath.h"

/* The directory the pick tests pretend holds ffprobe. */
static bool exec_in_bundle(const char *path) { return !strcmp(path, "/opt/xrdome/ffprobe"); }
static bool exec_nowhere(const char *path) { (void)path; return false; }

static void dirname_cases(void **state)
{
    (void)state;
    char b[64];
    assert_true(xrd_exepath_dirname("/a/b/xrdome", b, sizeof b));
    assert_string_equal(b, "/a/b");
    assert_true(xrd_exepath_dirname("/xrdome", b, sizeof b));
    assert_string_equal(b, "/");
    assert_false(xrd_exepath_dirname("xrdome", b, sizeof b));
    assert_string_equal(b, "");
    assert_false(xrd_exepath_dirname("/a/b/xrdome", b, 4)); /* "/a/b" needs 5 */
    assert_false(xrd_exepath_dirname(NULL, b, sizeof b));
}

static void configured_wins(void **state)
{
    (void)state;
    char o[64];
    assert_true(xrd_exepath_pick_ffprobe("/usr/bin/ffprobe", "/opt/xrdome", "/usr/bin", exec_in_bundle,
                                         o, sizeof o));
    assert_string_equal(o, "/usr/bin/ffprobe");
    /* An explicit "ffprobe" is a request for PATH, not for the bundle. */
    assert_true(xrd_exepath_pick_ffprobe("ffprobe", "/opt/xrdome", "/usr/bin", exec_in_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "ffprobe");
}

static void beside_the_executable(void **state)
{
    (void)state;
    char o[64];
    assert_true(xrd_exepath_pick_ffprobe(NULL, "/opt/xrdome", "/usr/bin:/bin", exec_in_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "/opt/xrdome/ffprobe");
    assert_true(xrd_exepath_pick_ffprobe("", "/opt/xrdome", NULL, exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "/opt/xrdome/ffprobe");
}

static void falls_back_to_path(void **state)
{
    (void)state;
    char o[64];
    /* No ffprobe beside it, directory unknown, no way to test. */
    assert_true(xrd_exepath_pick_ffprobe(NULL, "/opt/xrdome", "/usr/bin", exec_nowhere, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(NULL, "", "/usr/bin", exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(NULL, NULL, NULL, exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(NULL, "/opt/xrdome", "/usr/bin", NULL, o, sizeof o));
    assert_string_equal(o, "ffprobe");
}

static void directory_on_path_keeps_path_order(void **state)
{
    (void)state;
    char o[64];
    /* An install in a PATH directory: PATH's own order decides, as before. */
    assert_true(xrd_exepath_pick_ffprobe(NULL, "/opt/xrdome", "/usr/bin:/opt/xrdome", exec_in_bundle,
                                         o, sizeof o));
    assert_string_equal(o, "ffprobe");
    assert_true(xrd_exepath_pick_ffprobe(NULL, "/opt/xrdome", "/opt/xrdome/:/bin", exec_in_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "ffprobe");
    /* A prefix of an entry is not the entry. */
    assert_true(xrd_exepath_pick_ffprobe(NULL, "/opt/xrdome", "/opt/xrdome2:/opt", exec_in_bundle, o,
                                         sizeof o));
    assert_string_equal(o, "/opt/xrdome/ffprobe");
}

static void root_directory(void **state)
{
    (void)state;
    char o[64];
    assert_true(xrd_exepath_pick_ffprobe(NULL, "/", NULL, exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "ffprobe"); /* "/ffprobe" is not the stub's path */
}

static void too_small_output(void **state)
{
    (void)state;
    char o[8];
    assert_false(xrd_exepath_pick_ffprobe(NULL, "/opt/xrdome", "/bin", exec_in_bundle, o, sizeof o));
    assert_string_equal(o, "");
    assert_false(xrd_exepath_pick_ffprobe("/a/long/configured/path", NULL, NULL, NULL, o, sizeof o));
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
    assert_false(xrd_exepath_is_exec(f));
    assert_true(xrd_exepath_is_exec(x));
    assert_false(xrd_exepath_is_exec(dir)); /* a directory is searchable, not a program */
    assert_false(xrd_exepath_is_exec("/nonexistent/ffprobe"));

    /* The real pick, with the real check, finds a program beside "the executable". */
    char o[256], p[128];
    snprintf(p, sizeof p, "%s/ffprobe", dir);
    assert_int_equal(rename(x, p), 0);
    assert_true(xrd_exepath_pick_ffprobe(NULL, dir, "/usr/bin", xrd_exepath_is_exec, o, sizeof o));
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
