#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <cmocka.h>

#include "detect/probe.h"

#ifndef XRD_FIXTURE_DIR
#error "XRD_FIXTURE_DIR must be defined by the build"
#endif
#ifndef XRD_TESTS_DIR
#error "XRD_TESTS_DIR must be defined by the build"
#endif

#define FAKE XRD_TESTS_DIR "/fake_ffprobe.sh"

static char tmpdir[256];

static int group_setup(void **s)
{
    (void)s;
    snprintf(tmpdir, sizeof tmpdir, "/tmp/xrd_probe_run.XXXXXX");
    return mkdtemp(tmpdir) ? 0 : -1;
}

static int group_teardown(void **s)
{
    (void)s;
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmpdir);
    return system(cmd) == 0 ? 0 : -1;
}

static void make_file(const char *path)
{
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    fputs("not really a video\n", f);
    fclose(f);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void use_fake(const char *mode)
{
    setenv("FAKE_FFPROBE", mode, 1);
    setenv("FAKE_FFPROBE_FILE", XRD_FIXTURE_DIR "/sbs_mkv.txt", 1);
}

static void assert_no_children(void)
{
    errno = 0;
    assert_int_equal(waitpid(-1, NULL, WNOHANG), -1);
    assert_int_equal(errno, ECHILD);
}

static char *video(const char *name)
{
    static char path[512];
    snprintf(path, sizeof path, "%s/%s", tmpdir, name);
    make_file(path);
    return path;
}

static void run_ok(void **s)
{
    (void)s;
    use_fake("ok");
    xrd_probe p; char err[200];
    assert_int_equal(xrd_probe_run(FAKE, video("a.mkv"), 5000, &p, err, sizeof err), XRD_PROBE_OK);
    assert_true(p.have_size);
    assert_int_equal(p.width, 1920); assert_int_equal(p.height, 1080);
    assert_int_equal(p.stereo, XRD_STEREO_SBS);
    assert_no_children();
}

static void run_argv(void **s)
{
    (void)s;
    use_fake("argv");
    char out[512];
    snprintf(out, sizeof out, "%s/argv.txt", tmpdir);
    setenv("FAKE_FFPROBE_OUT", out, 1);
    const char *path = video("-weird \"name\".mp4");
    xrd_probe p; char err[200];
    assert_int_equal(xrd_probe_run(FAKE, path, 5000, &p, err, sizeof err), XRD_PROBE_OK);
    FILE *f = fopen(out, "r");
    assert_non_null(f);
    char line[600];
    const char *want[] = { "-v", "error", "-show_streams", "-i", path };
    for (size_t i = 0; i < 5; i++) {
        assert_non_null(fgets(line, sizeof line, f));
        line[strcspn(line, "\n")] = '\0';
        assert_string_equal(line, want[i]);
    }
    assert_null(fgets(line, sizeof line, f));
    fclose(f);
}

static void run_hang(void **s)
{
    (void)s;
    use_fake("hang");
    xrd_probe p; char err[200];
    double t0 = now_s();
    assert_int_equal(xrd_probe_run(FAKE, video("h.mkv"), 200, &p, err, sizeof err),
                     XRD_PROBE_FAILED);
    assert_true(now_s() - t0 < 1.0);
    assert_non_null(strstr(err, "timed out"));
    assert_false(p.have_size);
    assert_no_children();
}

static void run_flood(void **s)
{
    (void)s;
    use_fake("flood");
    xrd_probe p; char err[200];
    double t0 = now_s();
    assert_int_equal(xrd_probe_run(FAKE, video("f.mkv"), 5000, &p, err, sizeof err),
                     XRD_PROBE_FAILED);
    assert_true(now_s() - t0 < 2.0);
    assert_true(strlen(err) > 0);
    assert_no_children();
}

static void run_fail_and_segv(void **s)
{
    (void)s;
    xrd_probe p; char err[200];
    use_fake("fail");
    assert_int_equal(xrd_probe_run(FAKE, video("x.mkv"), 5000, &p, err, sizeof err),
                     XRD_PROBE_FAILED);
    assert_non_null(strstr(err, "status 3"));
    use_fake("segv");
    err[0] = '\0';
    assert_int_equal(xrd_probe_run(FAKE, video("x.mkv"), 5000, &p, err, sizeof err),
                     XRD_PROBE_FAILED);
    assert_true(strlen(err) > 0);
    assert_no_children();
}

static void run_no_video_stream(void **s)
{
    (void)s;
    use_fake("ok");
    setenv("FAKE_FFPROBE_FILE", XRD_FIXTURE_DIR "/audio_only.txt", 1);
    xrd_probe p; char err[200];
    assert_int_equal(xrd_probe_run(FAKE, video("n.mkv"), 5000, &p, err, sizeof err),
                     XRD_PROBE_FAILED);
    assert_non_null(strstr(err, "no video"));
}

static void run_missing_program(void **s)
{
    (void)s;
    xrd_probe p; char err[200];
    assert_int_equal(xrd_probe_run("/nonexistent/ffprobe", video("m.mkv"), 5000, &p, err,
                                   sizeof err), XRD_PROBE_FAILED);
    assert_non_null(strstr(err, "/nonexistent/ffprobe"));
    assert_no_children();
}

static void run_skipped(void **s)
{
    (void)s;
    use_fake("ok");
    xrd_probe p; char err[200] = "untouched";
    assert_int_equal(xrd_probe_run(FAKE, tmpdir, 5000, &p, err, sizeof err), XRD_PROBE_SKIPPED);
    char missing[512];
    snprintf(missing, sizeof missing, "%s/does-not-exist.mkv", tmpdir);
    assert_int_equal(xrd_probe_run(FAKE, missing, 5000, &p, err, sizeof err), XRD_PROBE_SKIPPED);
    assert_int_equal(xrd_probe_run(FAKE, "/dev/null", 5000, &p, err, sizeof err),
                     XRD_PROBE_SKIPPED);
    assert_no_children();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(run_ok),
        cmocka_unit_test(run_argv),
        cmocka_unit_test(run_hang),
        cmocka_unit_test(run_flood),
        cmocka_unit_test(run_fail_and_segv),
        cmocka_unit_test(run_no_video_stream),
        cmocka_unit_test(run_missing_program),
        cmocka_unit_test(run_skipped),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
