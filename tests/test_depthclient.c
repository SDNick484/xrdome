#define _XOPEN_SOURCE 700 /* mkdtemp, setenv, sockets */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <cmocka.h>

#include "app/depthclient.h"

static char g_dir[64];
static char g_sock[128];

static int setup(void **state)
{
    (void)state;
    strcpy(g_dir, "/tmp/xrd_dc_XXXXXX");
    assert_non_null(mkdtemp(g_dir));
    snprintf(g_sock, sizeof g_sock, "%s/s.sock", g_dir);
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
    return system(cmd) == 0 ? 0 : -1;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void nap_ms(int ms)
{
    struct timespec ts = {0, ms * 1000000L};
    nanosleep(&ts, NULL);
}

/* ---- paths ------------------------------------------------------------------ */

static void socket_path_follows_the_python_rule(void **state)
{
    (void)state;
    char buf[200], want[200];
    setenv("XDG_RUNTIME_DIR", "/run/user/7/", 1);
    assert_int_equal(xrd_depth_socket_path(buf, sizeof buf), 0);
    assert_string_equal(buf, "/run/user/7/xrdome-depth.sock");

    setenv("XDG_RUNTIME_DIR", "relative", 1);
    setenv("TMPDIR", "/var/t//", 1);
    assert_int_equal(xrd_depth_socket_path(buf, sizeof buf), 0);
    snprintf(want, sizeof want, "/var/t/xrdome-depth-%lu.sock", (unsigned long)getuid());
    assert_string_equal(buf, want);

    unsetenv("XDG_RUNTIME_DIR");
    unsetenv("TMPDIR");
    assert_int_equal(xrd_depth_socket_path(buf, sizeof buf), 0);
    snprintf(want, sizeof want, "/tmp/xrdome-depth-%lu.sock", (unsigned long)getuid());
    assert_string_equal(buf, want);

    setenv("TMPDIR", "/", 1);
    assert_int_equal(xrd_depth_socket_path(buf, sizeof buf), 0);
    snprintf(want, sizeof want, "/xrdome-depth-%lu.sock", (unsigned long)getuid());
    assert_string_equal(buf, want);

    /* Longer than a Unix socket address takes (about 104 bytes) */
    char run[200] = "/";
    memset(run + 1, 'x', 150);
    run[151] = '\0';
    setenv("XDG_RUNTIME_DIR", run, 1);
    assert_int_equal(xrd_depth_socket_path(buf, sizeof buf), -1);
    setenv("XDG_RUNTIME_DIR", "/run/user/7", 1);
    assert_int_equal(xrd_depth_socket_path(buf, 10), -1);
    unsetenv("XDG_RUNTIME_DIR");
    unsetenv("TMPDIR");
}

static void log_path(void **state)
{
    (void)state;
    char buf[64];
    assert_int_equal(xrd_depth_log_path("/run/xrdome-depth.sock", buf, sizeof buf), 0);
    assert_string_equal(buf, "/run/xrdome-depth.log");
    assert_int_equal(xrd_depth_log_path("/run/s", buf, sizeof buf), 0);
    assert_string_equal(buf, "/run/s.log");
    assert_int_equal(xrd_depth_log_path("/run/xrdome-depth.sock", buf, 8), -1);
}

/* ---- protocol ----------------------------------------------------------------- */

static void request_lines(void **state)
{
    (void)state;
    char buf[256];
    int n = xrd_depth_request_line("/p/a.jpg", false, buf, sizeof buf);
    assert_string_equal(buf, "{\"op\": \"photo\", \"path\": \"/p/a.jpg\", \"prefetch\": false}\n");
    assert_int_equal(n, (int)strlen(buf));
    xrd_depth_request_line("/p/\"q\"\\\n\x01" "caf\xe9.jpg", true, buf, sizeof buf);
    assert_string_equal(buf, "{\"op\": \"photo\", \"path\": \"/p/\\\"q\\\"\\\\\\u000a\\u0001caf\xe9.jpg\", "
                             "\"prefetch\": true}\n");
    assert_int_equal(xrd_depth_request_line("/p/a.jpg", false, buf, 40), -1);
    /* exactly fitting: the line and its NUL */
    n = xrd_depth_request_line("/a", false, buf, sizeof buf);
    assert_int_equal(xrd_depth_request_line("/a", false, buf, (size_t)n + 1), n);
    assert_int_equal(xrd_depth_request_line("/a", false, buf, (size_t)n), -1);
}

static bool parse(const char *s, xrd_depth_reply *r)
{
    return xrd_depth_reply_parse(s, strlen(s), r);
}

static void replies(void **state)
{
    (void)state;
    xrd_depth_reply r;
    assert_true(parse("{\"ok\": true, \"path\": \"/p/a.jpg\", \"depth\": \"/c/k-dav2s-v1.pgm\"}", &r));
    assert_true(r.ok);
    assert_string_equal(r.path, "/p/a.jpg");
    assert_string_equal(r.depth, "/c/k-dav2s-v1.pgm");

    assert_true(parse(" {\"ok\":false,\"path\":\"/a\",\"error\":\"no \\\"GPU\\\"\"} ", &r));
    assert_false(r.ok);
    assert_string_equal(r.error, "no \"GPU\"");
    assert_true(parse("{\"ok\": false, \"error\": \"bad\"}", &r)); /* no path: a protocol error */
    assert_string_equal(r.path, "");

    /* Unknown keys of any kind are skipped */
    assert_true(parse("{\"v\": 1.5e3, \"x\": [1, {\"y\": null}, \"]\"], \"ok\": true, "
                      "\"z\": {}, \"depth\": \"/d\", \"w\": -2, \"t\": false}", &r));
    assert_string_equal(r.depth, "/d");

    /* Escapes: UTF-8, a surrogate pair, and Python's surrogateescape bytes */
    assert_true(parse("{\"ok\": true, \"path\": \"/caf\\u00e9\\ud83d\\ude00\\udce9\\/\\t\", "
                      "\"depth\": \"/d\"}", &r));
    assert_string_equal(r.path, "/caf\xc3\xa9\xf0\x9f\x98\x80\xe9/\t");
    /* raw UTF-8 and raw bytes as they are */
    assert_true(parse("{\"ok\": true, \"path\": \"/caf\xc3\xa9\xe9\", \"depth\": \"/d\"}", &r));
    assert_string_equal(r.path, "/caf\xc3\xa9\xe9");

    /* A long error is cut, not refused */
    char line[2000];
    char msg[1200];
    memset(msg, 'e', sizeof msg - 1);
    msg[sizeof msg - 1] = '\0';
    snprintf(line, sizeof line, "{\"ok\": false, \"path\": \"/a\", \"error\": \"%s\"}", msg);
    assert_true(parse(line, &r));
    assert_int_equal(strlen(r.error), sizeof r.error - 1);

    static const char *bad[] = {
        "",
        "[]",
        "{}",                                            /* no ok */
        "{\"ok\": 1, \"depth\": \"/d\"}",                /* ok not a boolean */
        "{\"ok\": true}",                                /* ok without depth */
        "{\"ok\": true, \"depth\": \"\"}",
        "{\"ok\": true, \"depth\": \"/d\"} x",           /* trailing bytes */
        "{\"ok\": true, \"depth\": \"/d\"",              /* cut */
        "{\"ok\": true, \"depth\": \"/d\\q\"}",          /* bad escape */
        "{\"ok\": true, \"depth\": \"/d\\u12\"}",
        "{\"ok\": true, \"depth\": \"/d\\ud800\"}",      /* lone surrogate */
        "{\"ok\": true, \"depth\": \"/d\\u0000\"}",      /* NUL */
        "{\"ok\": true, \"depth\": \"/\td\"}",           /* raw control character */
        "{\"ok\": true \"depth\": \"/d\"}",              /* no comma */
        "{\"ok\": true, \"x\": [1 2], \"depth\": \"/d\"}",
        "{\"ok\": true, \"x\": tru, \"depth\": \"/d\"}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (parse(bad[i], &r))
            fail_msg("parsed: %s", bad[i]);
    }
    /* a path that does not fit is refused */
    char big[XRD_DEPTH_PATH_MAX + 100];
    char p[XRD_DEPTH_PATH_MAX + 10];
    memset(p, 'p', sizeof p - 1);
    p[sizeof p - 1] = '\0';
    snprintf(big, sizeof big, "{\"ok\": true, \"path\": \"%s\", \"depth\": \"/d\"}", p);
    assert_false(parse(big, &r));
}

/* ---- the client against a fake server ---------------------------------------------- */

static int listen_on(const char *path)
{
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    assert_true(s >= 0);
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    assert_int_equal(bind(s, (struct sockaddr *)&sa, sizeof sa), 0);
    assert_int_equal(listen(s, 4), 0);
    return s;
}

/* Reads until want bytes came (blocking, the client sends without waiting). */
static void read_exactly(int fd, char *buf, size_t want)
{
    size_t got = 0;
    while (got < want) {
        ssize_t n = read(fd, buf + got, want - got);
        assert_true(n > 0);
        got += (size_t)n;
    }
    buf[got] = '\0';
}

static xrd_dc_event poll_until(xrd_depthclient *dc, xrd_depth_reply *r, int ms)
{
    xrd_dc_event ev = XRD_DC_NONE;
    for (int i = 0; i < ms && (ev = xrd_depthclient_poll(dc, now_ns(), r)) == XRD_DC_NONE; i++)
        nap_ms(1);
    return ev;
}

static void talks_to_a_running_helper(void **state)
{
    (void)state;
    int ls = listen_on(g_sock);
    xrd_depthclient *dc = xrd_depthclient_new(g_sock, NULL);
    assert_non_null(dc);
    xrd_depth_reply r;
    assert_int_equal(xrd_depthclient_poll(dc, now_ns(), &r), XRD_DC_NONE); /* idle: nothing asked */
    assert_true(xrd_depthclient_request(dc, "/p/a.jpg", false));
    assert_true(xrd_depthclient_request(dc, "/p/b.jpg", true));
    assert_int_equal(poll_until(dc, &r, 20), XRD_DC_NONE); /* connects and sends */
    int c = accept(ls, NULL, NULL);
    assert_true(c >= 0);
    char want[512], got[512];
    int n1 = xrd_depth_request_line("/p/a.jpg", false, want, sizeof want);
    int n2 = xrd_depth_request_line("/p/b.jpg", true, want + n1, sizeof want - (size_t)n1);
    read_exactly(c, got, (size_t)(n1 + n2));
    assert_string_equal(got, want);
    assert_false(xrd_depthclient_started(dc));

    /* Two replies and half of a third in one write; the rest later. */
    const char *part1 = "{\"ok\": true, \"path\": \"/p/b.jpg\", \"depth\": \"/c/b.pgm\"}\n"
                        "garbage\n"
                        "{\"ok\": false, \"path\": \"/p/a.jpg\", \"error\": \"no GPU\"}\n"
                        "{\"ok\": true, \"pa";
    assert_int_equal(write(c, part1, strlen(part1)), (ssize_t)strlen(part1));
    assert_int_equal(poll_until(dc, &r, 2000), XRD_DC_REPLY);
    assert_true(r.ok);
    assert_string_equal(r.path, "/p/b.jpg");
    assert_string_equal(r.depth, "/c/b.pgm");
    assert_int_equal(xrd_depthclient_poll(dc, now_ns(), &r), XRD_DC_REPLY); /* garbage skipped */
    assert_false(r.ok);
    assert_string_equal(r.error, "no GPU");
    assert_int_equal(xrd_depthclient_poll(dc, now_ns(), &r), XRD_DC_NONE);
    const char *part2 = "th\": \"/p/c.jpg\", \"depth\": \"/c/c.pgm\"}\n";
    assert_int_equal(write(c, part2, strlen(part2)), (ssize_t)strlen(part2));
    assert_int_equal(poll_until(dc, &r, 2000), XRD_DC_REPLY);
    assert_string_equal(r.path, "/p/c.jpg");

    /* The helper goes away: one FAILED, then nothing, and requests are refused. */
    close(c);
    assert_int_equal(poll_until(dc, &r, 2000), XRD_DC_FAILED);
    assert_true(xrd_depthclient_failed(dc));
    assert_non_null(strstr(xrd_depthclient_error(dc), "closed the connection"));
    assert_int_equal(xrd_depthclient_poll(dc, now_ns(), &r), XRD_DC_NONE);
    assert_false(xrd_depthclient_request(dc, "/p/d.jpg", false));
    xrd_depthclient_free(dc);
    close(ls);
}

static void video_lines_and_replies(void **state)
{
    (void)state;
    char buf[128];
    int n = xrd_depth_video_line("/xrdome-42-1", buf, sizeof buf);
    assert_string_equal(buf, "{\"op\": \"video\", \"shm\": \"/xrdome-42-1\"}\n");
    assert_int_equal(n, (int)strlen(buf));
    assert_int_equal(xrd_depth_video_line("/a\"b", buf, sizeof buf), -1);
    assert_int_equal(xrd_depth_video_line("/xrdome-42-1", buf, 10), -1);

    xrd_depth_reply r;
    assert_true(parse("{\"ok\": true, \"op\": \"video\", \"shm\": \"/xrdome-42-1\"}", &r));
    assert_true(r.ok && r.video);
    assert_string_equal(r.shm, "/xrdome-42-1");
    assert_true(parse("{\"ok\": false, \"op\": \"video\", \"shm\": \"/x\", \"error\": \"no\"}", &r));
    assert_true(!r.ok && r.video);
    assert_string_equal(r.error, "no");
    assert_false(parse("{\"ok\": true, \"op\": \"video\"}", &r)); /* which ring? */
    assert_false(parse("{\"ok\": false, \"op\": \"video\", \"error\": \"x\"}", &r));
    /* a photo reply is not a video one, whatever else it carries */
    assert_true(parse("{\"ok\": true, \"op\": \"photo\", \"path\": \"/a\", \"depth\": \"/d\"}", &r));
    assert_false(r.video);
}

static void sends_a_video_request(void **state)
{
    (void)state;
    int ls = listen_on(g_sock);
    xrd_depthclient *dc = xrd_depthclient_new(g_sock, NULL);
    assert_non_null(dc);
    xrd_depth_reply r;
    assert_true(xrd_depthclient_video(dc, "/xrdome-42-1"));
    assert_int_equal(poll_until(dc, &r, 20), XRD_DC_NONE);
    int c = accept(ls, NULL, NULL);
    assert_true(c >= 0);
    char want[128], got[128];
    int n = xrd_depth_video_line("/xrdome-42-1", want, sizeof want);
    read_exactly(c, got, (size_t)n);
    assert_string_equal(got, want);
    const char *rep = "{\"ok\": true, \"op\": \"video\", \"shm\": \"/xrdome-42-1\"}\n";
    assert_int_equal(write(c, rep, strlen(rep)), (ssize_t)strlen(rep));
    assert_int_equal(poll_until(dc, &r, 2000), XRD_DC_REPLY);
    assert_true(r.ok && r.video);
    close(c);
    xrd_depthclient_free(dc);
    close(ls);
}

static void writing_to_a_closed_helper_does_not_kill(void **state)
{
    (void)state;
    int ls = listen_on(g_sock);
    xrd_depthclient *dc = xrd_depthclient_new(g_sock, NULL);
    xrd_depth_reply r;
    assert_true(xrd_depthclient_request(dc, "/p/a.jpg", false));
    assert_int_equal(xrd_depthclient_poll(dc, now_ns(), &r), XRD_DC_NONE);
    int c = accept(ls, NULL, NULL);
    close(c);
    close(ls);
    /* Large writes into a closed peer: EPIPE, not SIGPIPE. */
    char path[3000];
    memset(path, 'x', sizeof path - 1);
    path[0] = '/';
    path[sizeof path - 1] = '\0';
    xrd_dc_event ev = XRD_DC_NONE;
    for (int i = 0; i < 200 && ev == XRD_DC_NONE; i++) {
        xrd_depthclient_request(dc, path, true);
        ev = xrd_depthclient_poll(dc, now_ns(), &r);
    }
    assert_int_equal(ev, XRD_DC_FAILED);
    xrd_depthclient_free(dc);
}

static void no_helper_and_no_socket(void **state)
{
    (void)state;
    xrd_depthclient *dc = xrd_depthclient_new(g_sock, NULL);
    xrd_depth_reply r;
    assert_true(xrd_depthclient_request(dc, "/p/a.jpg", false));
    assert_int_equal(xrd_depthclient_poll(dc, now_ns(), &r), XRD_DC_FAILED);
    char want[256];
    snprintf(want, sizeof want, "no depth helper answers on %s", g_sock);
    assert_string_equal(xrd_depthclient_error(dc), want);
    xrd_depthclient_free(dc);
}

static void a_helper_that_cannot_start(void **state)
{
    (void)state;
    char helper[128];
    snprintf(helper, sizeof helper, "%s/no-such-helper", g_dir);
    xrd_depthclient *dc = xrd_depthclient_new(g_sock, helper);
    xrd_depth_reply r;
    assert_true(xrd_depthclient_request(dc, "/p/a.jpg", false));
    assert_int_equal(poll_until(dc, &r, 5000), XRD_DC_FAILED);
    /* posix_spawnp reports it, or the child exits 127 */
    const char *e = xrd_depthclient_error(dc);
    assert_non_null(strstr(e, helper));
    assert_true(strstr(e, "cannot start") || strstr(e, "status 127"));
    xrd_depthclient_free(dc);
}

static void a_helper_that_exits(void **state)
{
    (void)state;
    char helper[128], logp[128];
    snprintf(helper, sizeof helper, "%s/helper.sh", g_dir);
    FILE *f = fopen(helper, "w");
    assert_non_null(f);
    fputs("#!/bin/sh\necho \"started: $*\"\necho 'depth serve: no GPU' >&2\nexit 2\n", f);
    fclose(f);
    assert_int_equal(chmod(helper, 0755), 0);
    xrd_depthclient *dc = xrd_depthclient_new(g_sock, helper);
    xrd_depth_reply r;
    assert_true(xrd_depthclient_request(dc, "/p/a.jpg", false));
    assert_int_equal(poll_until(dc, &r, 5000), XRD_DC_FAILED);
    assert_true(xrd_depthclient_started(dc));
    snprintf(logp, sizeof logp, "%s/s.log", g_dir);
    char want[512];
    snprintf(want, sizeof want, "%s depth serve exited with status 2 (its log: %s)", helper, logp);
    assert_string_equal(xrd_depthclient_error(dc), want);
    /* Its arguments, and its stdout and stderr in the log */
    char log[512] = "";
    f = fopen(logp, "r");
    assert_non_null(f);
    size_t n = fread(log, 1, sizeof log - 1, f);
    log[n] = '\0';
    fclose(f);
    char args[512];
    snprintf(args, sizeof args, "started: depth serve --socket %s\ndepth serve: no GPU\n", g_sock);
    assert_string_equal(log, args);
    xrd_depthclient_free(dc);
}

static void a_started_helper_is_waited_for(void **state)
{
    (void)state;
    /* A helper that opens the socket late: the client keeps trying. */
    char helper[128];
    snprintf(helper, sizeof helper, "%s/late.sh", g_dir);
    FILE *f = fopen(helper, "w");
    assert_non_null(f);
    fputs("#!/bin/sh\nsleep 3\n", f);
    fclose(f);
    assert_int_equal(chmod(helper, 0755), 0);
    xrd_depthclient *dc = xrd_depthclient_new(g_sock, helper);
    xrd_depth_reply r;
    assert_true(xrd_depthclient_request(dc, "/p/a.jpg", false));
    assert_int_equal(poll_until(dc, &r, 200), XRD_DC_NONE);
    assert_true(xrd_depthclient_started(dc));
    int ls = listen_on(g_sock); /* as if the helper bound it now */
    assert_int_equal(poll_until(dc, &r, 200), XRD_DC_NONE);
    int c = accept(ls, NULL, NULL);
    char got[256], want[256];
    int n = xrd_depth_request_line("/p/a.jpg", false, want, sizeof want);
    read_exactly(c, got, (size_t)n);
    assert_string_equal(got, want);
    close(c);
    close(ls);
    xrd_depthclient_free(dc);
    /* The sleeping script is left to finish on its own, as a real helper would be. */
}

static void a_helper_that_never_answers_times_out(void **state)
{
    (void)state;
    char helper[128];
    snprintf(helper, sizeof helper, "%s/mute.sh", g_dir);
    FILE *f = fopen(helper, "w");
    assert_non_null(f);
    fputs("#!/bin/sh\nsleep 5\n", f);
    fclose(f);
    assert_int_equal(chmod(helper, 0755), 0);
    xrd_depthclient *dc = xrd_depthclient_new(g_sock, helper);
    xrd_depth_reply r;
    assert_true(xrd_depthclient_request(dc, "/p/a.jpg", false));
    uint64_t t = now_ns();
    assert_int_equal(xrd_depthclient_poll(dc, t, &r), XRD_DC_NONE);
    assert_true(xrd_depthclient_started(dc));
    /* The clock passed in is what counts. */
    assert_int_equal(xrd_depthclient_poll(dc, t + XRD_DEPTH_CONNECT_NS + 1, &r), XRD_DC_FAILED);
    assert_non_null(strstr(xrd_depthclient_error(dc), "did not open"));
    xrd_depthclient_free(dc);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(socket_path_follows_the_python_rule),
        cmocka_unit_test(log_path),
        cmocka_unit_test(request_lines),
        cmocka_unit_test(video_lines_and_replies),
        cmocka_unit_test_setup_teardown(sends_a_video_request, setup, teardown),
        cmocka_unit_test(replies),
        cmocka_unit_test_setup_teardown(talks_to_a_running_helper, setup, teardown),
        cmocka_unit_test_setup_teardown(writing_to_a_closed_helper_does_not_kill, setup, teardown),
        cmocka_unit_test_setup_teardown(no_helper_and_no_socket, setup, teardown),
        cmocka_unit_test_setup_teardown(a_helper_that_cannot_start, setup, teardown),
        cmocka_unit_test_setup_teardown(a_helper_that_exits, setup, teardown),
        cmocka_unit_test_setup_teardown(a_started_helper_is_waited_for, setup, teardown),
        cmocka_unit_test_setup_teardown(a_helper_that_never_answers_times_out, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
