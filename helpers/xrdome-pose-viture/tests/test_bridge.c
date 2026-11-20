#define _POSIX_C_SOURCE 200809L /* sockets, setenv */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "bridge.h"
#include "pose/pose.h"
#include "test_compat.h"
#include "util/timing.h"

/* The helper's whole session, in this process: the real loader opens the fake
 * SDK, the bridge runs on a thread, and xrdome's own udp-opentrack reader
 * (src/pose/pose_udp_opentrack.c) receives what it sends. */

#ifdef _WIN32
#define sock_close(fd) closesocket((SOCKET)(fd))
#else
#define sock_close(fd) close(fd)
#endif

static char log_path[64];
static char out_path[64];

static int setup(void **state)
{
    (void)state;
    snprintf(log_path, sizeof log_path, "/tmp/xrd-viture-log-XXXXXX");
    snprintf(out_path, sizeof out_path, "/tmp/xrd-viture-out-XXXXXX");
    int fd = mkstemp(log_path);
    if (fd < 0)
        return -1;
    close(fd);
    fd = mkstemp(out_path);
    if (fd < 0)
        return -1;
    close(fd);
    setenv("XRD_FAKE_VITURE_LOG", log_path, 1);
    unsetenv("XRD_FAKE_VITURE_MODE");
    unsetenv("XRD_FAKE_VITURE_POSE");
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    unsetenv("XRD_FAKE_VITURE_LOG");
    unsetenv("XRD_FAKE_VITURE_MODE");
    unsetenv("XRD_FAKE_VITURE_POSE");
    remove(log_path);
    remove(out_path);
    return 0;
}

static int free_port(void)
{
    int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    assert_true(fd >= 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert_int_equal(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
    socklen_t l = sizeof a;
    assert_int_equal(getsockname(fd, (struct sockaddr *)&a, &l), 0);
    int port = ntohs(a.sin_port);
    sock_close(fd);
    return port;
}

static void slurp(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "rb");
    assert_non_null(f);
    size_t n = fread(buf, 1, len - 1, f);
    buf[n] = '\0';
    fclose(f);
}

/* text contains first, and then second somewhere after it. */
static void assert_in_order(const char *text, const char *first, const char *second)
{
    const char *a = strstr(text, first);
    if (!a)
        fail_msg("'%s' missing from:\n%s", first, text);
    if (!strstr(a + strlen(first), second))
        fail_msg("'%s' not after '%s' in:\n%s", second, first, text);
}

typedef struct {
    viture_options o;
    viture_api api;
    int pid;
    atomic_int stop;
    FILE *out;
    long sent;
    int rc;
    pthread_t thread;
} run;

static void *run_thread(void *arg)
{
    run *r = arg;
    r->rc = viture_bridge_run(&r->o, &r->api, r->pid, &r->stop, r->out, &r->sent);
    return NULL;
}

static void start_run(run *r, int port, int pid)
{
    memset(r, 0, sizeof *r);
    r->o.host = "127.0.0.1";
    r->o.port = port;
    r->o.rate = 250;
    r->pid = pid;
    atomic_init(&r->stop, 0);
    char err[200];
    assert_int_equal(viture_load(XRD_FAKE_SDK, &r->api, err, sizeof err), 0);
    r->out = fopen(out_path, "w+");
    assert_non_null(r->out);
}

static void launch(run *r)
{
    assert_int_equal(pthread_create(&r->thread, NULL, run_thread, r), 0);
}

static void finish(run *r)
{
    atomic_store(&r->stop, 1);
    pthread_join(r->thread, NULL);
    fclose(r->out);
    viture_unload(&r->api);
}

static xrd_pose_source *reader(int port)
{
    char spec[64], err[128] = "";
    snprintf(spec, sizeof spec, "udp-opentrack:port=%d", port);
    xrd_pose_source *s = xrd_pose_source_create(spec, err, sizeof err);
    if (!s)
        fail_msg("udp-opentrack: %s", err);
    return s;
}

static bool wait_pose(xrd_pose_source *s, xrd_pose *p)
{
    uint64_t end = xrd_now_ns() + 3000000000ull;
    do {
        if (s->latest(s, p) == 0)
            return true;
        xrd_sleep_ns(1000000);
    } while (xrd_now_ns() < end);
    return false;
}

/* The pose the fake reports: NWU yaw 30, pitch -10, roll 5 (its default), as
 * a quaternion built here from axis-angle pieces, then carried into xrdome's
 * GL axes (x right, y up, -z forward) by a change of basis: x_gl = -y_nwu,
 * y_gl = z_nwu, z_gl = -x_nwu. */
static xrd_quat expected_gl(void)
{
    const double d2r = 3.14159265358979323846 / 180.0;
    double q[3][4]; /* yaw, pitch, roll as w,x,y,z */
    const double deg[3] = {30, -10, 5};
    const int ax[3][3] = {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
    for (int i = 0; i < 3; i++) {
        double h = deg[i] * d2r / 2;
        q[i][0] = cos(h);
        for (int k = 0; k < 3; k++)
            q[i][k + 1] = ax[i][k] * sin(h);
    }
#define MUL(a, b, o) \
    do { \
        (o)[0] = (a)[0] * (b)[0] - (a)[1] * (b)[1] - (a)[2] * (b)[2] - (a)[3] * (b)[3]; \
        (o)[1] = (a)[0] * (b)[1] + (a)[1] * (b)[0] + (a)[2] * (b)[3] - (a)[3] * (b)[2]; \
        (o)[2] = (a)[0] * (b)[2] - (a)[1] * (b)[3] + (a)[2] * (b)[0] + (a)[3] * (b)[1]; \
        (o)[3] = (a)[0] * (b)[3] + (a)[1] * (b)[2] - (a)[2] * (b)[1] + (a)[3] * (b)[0]; \
    } while (0)
    double yp[4], n[4];
    MUL(q[0], q[1], yp);
    MUL(yp, q[2], n);
    xrd_quat g = {(float)n[0], (float)-n[2], (float)n[3], (float)-n[1]};
    return g;
}

static void assert_same_rotation(xrd_quat a, xrd_quat b)
{
    double dot = (double)a.w * b.w + (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z;
    assert_true(fabs(fabs(dot) - 1.0) < 1e-4);
}

static void pose_reaches_xrdome(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *src = reader(port);
    run r;
    start_run(&r, port, 0x101D);
    launch(&r);

    xrd_pose p;
    assert_true(wait_pose(src, &p));
    assert_same_rotation(p.q, expected_gl());
    /* Samples keep coming. */
    uint32_t first = p.seq;
    uint64_t end = xrd_now_ns() + 3000000000ull;
    while (xrd_now_ns() < end && (src->latest(src, &p) != 0 || p.seq < first + 5))
        xrd_sleep_ns(1000000);
    assert_true(p.seq >= first + 5);
    assert_same_rotation(p.q, expected_gl());
    assert_int_equal(xrd_pose_udp_dropped(src), 0);

    finish(&r);
    xrd_pose_source_destroy(src);
    assert_int_equal(r.rc, 0);
    assert_true(r.sent >= 6);

    char log[2048];
    slurp(log_path, log, sizeof log);
    assert_in_order(log, "create 4125", "initialize");
    assert_in_order(log, "initialize", "start");
    assert_in_order(log, "start", "register_imu_pose callback");
    assert_in_order(log, "register_imu_pose", "open_imu 1 ");
    assert_in_order(log, "open_imu", "close_imu 1");
    assert_in_order(log, "close_imu", "stop");
    assert_in_order(log, "stop", "shutdown");
    assert_in_order(log, "shutdown", "destroy");
    assert_null(strstr(log, "display_mode"));
}

static void packet_count_limit_ends_the_session(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *src = reader(port);
    run r;
    start_run(&r, port, 0x1019);
    r.o.count = 12;
    launch(&r);
    pthread_join(r.thread, NULL); /* ends by itself */
    assert_int_equal(r.rc, 0);
    assert_int_equal(r.sent, 12);

    xrd_pose p;
    assert_true(wait_pose(src, &p));
    fclose(r.out);
    viture_unload(&r.api);
    xrd_pose_source_destroy(src);
}

static void sbs_switches_and_restores(void **state)
{
    (void)state;
    setenv("XRD_FAKE_VITURE_MODE", "0x34", 1); /* 1920x1080 at 120 Hz */
    int port = free_port();
    run r;
    start_run(&r, port, 0x1019);
    r.o.sbs = true;
    r.o.count = 5;
    launch(&r);
    pthread_join(r.thread, NULL);
    assert_int_equal(r.rc, 0);
    fclose(r.out);
    viture_unload(&r.api);

    char log[2048];
    slurp(log_path, log, sizeof log);
    assert_in_order(log, "start", "get_display_mode");
    assert_in_order(log, "get_display_mode", "set_display_mode 50\n"); /* 0x32 */
    assert_in_order(log, "set_display_mode 50\n", "open_imu");
    assert_in_order(log, "close_imu", "set_display_mode 52\n"); /* back to 0x34 */
    assert_in_order(log, "set_display_mode 52\n", "stop");
    assert_in_order(log, "stop", "destroy");
}

static void sbs_leaves_a_3840_mode_alone(void **state)
{
    (void)state;
    setenv("XRD_FAKE_VITURE_MODE", "0x32", 1);
    int port = free_port();
    run r;
    start_run(&r, port, 0x1019);
    r.o.sbs = true;
    r.o.count = 3;
    launch(&r);
    pthread_join(r.thread, NULL);
    assert_int_equal(r.rc, 0);
    fclose(r.out);
    viture_unload(&r.api);

    char log[2048];
    slurp(log_path, log, sizeof log);
    assert_non_null(strstr(log, "get_display_mode"));
    assert_null(strstr(log, "set_display_mode"));
}

static void restores_when_stopped_from_outside(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *src = reader(port);
    run r;
    start_run(&r, port, 0x1019);
    r.o.sbs = true; /* the fake starts in 0x31 */
    launch(&r);
    xrd_pose p;
    assert_true(wait_pose(src, &p));
    finish(&r); /* what Ctrl+C does: sets the stop flag */
    xrd_pose_source_destroy(src);
    assert_int_equal(r.rc, 0);

    char log[2048];
    slurp(log_path, log, sizeof log);
    assert_in_order(log, "set_display_mode 50\n", "close_imu");
    assert_in_order(log, "close_imu", "set_display_mode 49\n");
}

static char *read_out(char *buf, size_t len)
{
    slurp(out_path, buf, len);
    return buf;
}

static void carina_is_refused(void **state)
{
    (void)state;
    run r;
    start_run(&r, free_port(), 0x7001);
    launch(&r);
    pthread_join(r.thread, NULL);
    fclose(r.out);
    viture_unload(&r.api);
    assert_int_equal(r.rc, 1);
    char out[1024], log[1024];
    assert_non_null(strstr(read_out(out, sizeof out), "Carina"));
    slurp(log_path, log, sizeof log);
    assert_null(strstr(log, "open_imu"));
    assert_null(strstr(log, "start\n"));
    assert_in_order(log, "create", "destroy");
}

static void glasses_that_cannot_be_opened(void **state)
{
    (void)state;
    run r;
    start_run(&r, free_port(), 0x1234);
    launch(&r);
    pthread_join(r.thread, NULL);
    fclose(r.out);
    viture_unload(&r.api);
    assert_int_equal(r.rc, 1);
    char out[1024];
    assert_non_null(strstr(read_out(out, sizeof out), "could not open the glasses"));
}

static void bad_host_fails_before_the_sdk_is_touched(void **state)
{
    (void)state;
    run r;
    start_run(&r, free_port(), 0x1019);
    r.o.host = "not-an-address";
    launch(&r);
    pthread_join(r.thread, NULL);
    fclose(r.out);
    viture_unload(&r.api);
    assert_int_equal(r.rc, 1);
    char out[1024], log[1024];
    assert_non_null(strstr(read_out(out, sizeof out), "--host"));
    slurp(log_path, log, sizeof log);
    assert_string_equal(log, "");
}

static void rate_cap_limits_packets(void **state)
{
    (void)state;
    setenv("XRD_FAKE_VITURE_HZ", "500", 1);
    int port = free_port();
    run r;
    start_run(&r, port, 0x1019);
    r.o.rate = 50;
    uint64_t t0 = xrd_now_ns();
    launch(&r);
    xrd_sleep_ns(600000000ull); /* 0.6 s: about 31 packets at 50 Hz, 300 uncapped */
    finish(&r);
    uint64_t elapsed_ns = xrd_now_ns() - t0;
    unsetenv("XRD_FAKE_VITURE_HZ");
    assert_int_equal(r.rc, 0);
    assert_true(r.sent >= 1);
    /* The cap is a schedule of one packet every 1/rate s, the first at once, so a run of T
     * seconds sends at most T * rate + 1. T is measured here (launch to the end of finish, which
     * contains every send) and not taken to be 0.6: a loaded CI runner can stretch the sleep, and
     * a 0.9 s run honestly sends 46. One more packet is allowed for the clock read. An uncapped
     * stream (500 Hz here) is about ten times over this bound. */
    long most = (long)((double)elapsed_ns / 1e9 * r.o.rate) + 2;
    if (r.sent > most)
        fail_msg("%ld packets in %.3f s at a cap of %g Hz: at most %ld expected", r.sent,
                 (double)elapsed_ns / 1e9, r.o.rate, most);
}

static void frequency_choice(void **state)
{
    (void)state;
    /* Bits 0..5: 60, 90, 120, 240, 500, 1000 Hz. */
    assert_int_equal(viture_pick_frequency(250, 0x3F), 4); /* 500 Hz: twice 250 */
    assert_int_equal(viture_pick_frequency(250, 0x1F), 4);
    assert_int_equal(viture_pick_frequency(50, 0x3F), 2);  /* 120 Hz */
    assert_int_equal(viture_pick_frequency(30, 0x3F), 0);  /* 60 Hz */
    assert_int_equal(viture_pick_frequency(250, 0x0F), 3); /* nothing faster: the highest */
    assert_int_equal(viture_pick_frequency(1000, 0x3F), 5);
    assert_int_equal(viture_pick_frequency(250, 0x20), 5);
    assert_int_equal(viture_pick_frequency(250, 0), -1);
}

static void sbs_target_choice(void **state)
{
    (void)state;
    int t = 0;
    assert_true(viture_sbs_target(0x31, &t));
    assert_int_equal(t, 0x32);
    assert_true(viture_sbs_target(0x34, &t)); /* 1080p 120 Hz */
    assert_int_equal(t, 0x32);
    t = 0;
    assert_false(viture_sbs_target(0x32, &t));
    assert_false(viture_sbs_target(0x35, &t));
    assert_int_equal(t, 0);
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 1;
#endif
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(frequency_choice),
        cmocka_unit_test(sbs_target_choice),
        cmocka_unit_test_setup_teardown(pose_reaches_xrdome, setup, teardown),
        cmocka_unit_test_setup_teardown(packet_count_limit_ends_the_session, setup, teardown),
        cmocka_unit_test_setup_teardown(sbs_switches_and_restores, setup, teardown),
        cmocka_unit_test_setup_teardown(sbs_leaves_a_3840_mode_alone, setup, teardown),
        cmocka_unit_test_setup_teardown(restores_when_stopped_from_outside, setup, teardown),
        cmocka_unit_test_setup_teardown(carina_is_refused, setup, teardown),
        cmocka_unit_test_setup_teardown(glasses_that_cannot_be_opened, setup, teardown),
        cmocka_unit_test_setup_teardown(bad_host_fails_before_the_sdk_is_touched, setup, teardown),
        cmocka_unit_test_setup_teardown(rate_cap_limits_packets, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
