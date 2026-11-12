#define _POSIX_C_SOURCE 200809L /* sockets, clock_gettime */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <cmocka.h>

#include "pose/opentrack_packet.h"
#include "pose/pose.h"
#include "util/timing.h"

#define TOL 1e-4
#define DEADLINE_NS 200000000ull /* 200 ms */

static int bind_loopback(int port, int *bound_port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert_true(fd >= 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    socklen_t l = sizeof a;
    assert_int_equal(getsockname(fd, (struct sockaddr *)&a, &l), 0);
    *bound_port = ntohs(a.sin_port);
    return fd;
}

/* A port that was free a moment ago. */
static int free_port(void)
{
    int port;
    int fd = bind_loopback(0, &port);
    assert_true(fd >= 0);
    close(fd);
    return port;
}

static void send_bytes(int port, const void *buf, size_t len)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert_true(fd >= 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    assert_int_equal((int)sendto(fd, buf, len, 0, (struct sockaddr *)&a, sizeof a), (int)len);
    close(fd);
}

static void send_ypr(int port, double yaw, double pitch, double roll, uint32_t frame)
{
    uint8_t buf[XRD_OT_PACKET_SIZE];
    const double d[6] = {0, 0, 0, yaw, pitch, roll};
    memcpy(buf, d, 48);
    memcpy(buf + 48, &frame, 4);
    send_bytes(port, buf, sizeof buf);
}

/* Polls latest() until it reports a pose with seq >= min_seq or the deadline
 * passes. Returns true on success. */
static bool wait_pose(xrd_pose_source *s, uint32_t min_seq, xrd_pose *out)
{
    uint64_t end = xrd_now_ns() + DEADLINE_NS;
    do {
        if (s->latest(s, out) == 0 && out->seq >= min_seq)
            return true;
        struct timespec ts = {0, 500000}; /* 0.5 ms */
        nanosleep(&ts, NULL);
    } while (xrd_now_ns() < end);
    return false;
}

static xrd_pose_source *make_udp(int port, const char *extra)
{
    char spec[128], err[128] = "";
    snprintf(spec, sizeof spec, "udp-opentrack:port=%d%s%s", port, extra[0] ? "," : "", extra);
    xrd_pose_source *s = xrd_pose_source_create(spec, err, sizeof err);
    if (!s)
        fail_msg("create failed: %s", err);
    return s;
}

static void expect_vec(xrd_quat q, float x, float y, float z, const float in[3])
{
    float v[3];
    xrd_quat_rotate(q, in, v);
    assert_float_equal(v[0], x, TOL);
    assert_float_equal(v[1], y, TOL);
    assert_float_equal(v[2], z, TOL);
}

static const float FWD[3] = {0, 0, -1};
static const float UP[3] = {0, 1, 0};

static void no_data_returns_1(void **state)
{
    (void)state;
    xrd_pose_source *s = make_udp(free_port(), "");
    xrd_pose p;
    assert_int_equal(s->latest(s, &p), 1);
    xrd_pose_source_destroy(s);
}

static void packet_updates_pose(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "");
    uint64_t before = xrd_now_ns();
    send_ypr(port, 90, 0, 0, 1);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    expect_vec(p.q, -1, 0, 0, FWD);
    assert_true(p.t_mono_ns >= before);
    assert_true(p.t_mono_ns <= xrd_now_ns());
    xrd_pose_source_destroy(s);
}

static void nwu_pitch_maps_up(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "");
    send_ypr(port, 0, -30, 0, 1);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    expect_vec(p.q, 0, 0.5f, -0.8660254f, FWD);
    xrd_pose_source_destroy(s);
}

static void nwu_roll_maps(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "");
    send_ypr(port, 0, 0, 90, 1);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    expect_vec(p.q, 1, 0, 0, UP);
    xrd_pose_source_destroy(s);
}

static void sign_option_applies(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "yaw=-1");
    send_ypr(port, 90, 0, 0, 1);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    expect_vec(p.q, 1, 0, 0, FWD);
    xrd_pose_source_destroy(s);
}

static void pitch_and_roll_sign_options_apply(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "pitch=-1,roll=-1");
    send_ypr(port, 0, -30, 0, 1);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    expect_vec(p.q, 0, -0.5f, -0.8660254f, FWD);
    send_ypr(port, 0, 0, 90, 2);
    assert_true(wait_pose(s, 2, &p));
    expect_vec(p.q, -1, 0, 0, UP);
    xrd_pose_source_destroy(s);
}

static void bad_size_counted(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "");
    assert_int_equal(xrd_pose_udp_dropped(s), 0);
    uint8_t junk[10] = {0};
    send_bytes(port, junk, sizeof junk);
    uint64_t end = xrd_now_ns() + DEADLINE_NS;
    while (xrd_pose_udp_dropped(s) != 1 && xrd_now_ns() < end) {
        struct timespec ts = {0, 500000};
        nanosleep(&ts, NULL);
    }
    assert_int_equal(xrd_pose_udp_dropped(s), 1);
    xrd_pose p;
    assert_int_equal(s->latest(s, &p), 1);
    xrd_pose_source_destroy(s);
}

static void frame_backwards_accepted(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "");
    send_ypr(port, 10, 0, 0, 100);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    send_ypr(port, 20, 0, 0, 3);
    assert_true(wait_pose(s, 2, &p));
    const double r = 20.0 * 3.14159265358979323846 / 180.0;
    expect_vec(p.q, (float)-sin(r), 0, (float)-cos(r), FWD);
    assert_int_equal(xrd_pose_udp_dropped(s), 0);
    xrd_pose_source_destroy(s);
}

static void port_in_use_fails(void **state)
{
    (void)state;
    int port;
    int holder = bind_loopback(0, &port);
    assert_true(holder >= 0);
    char spec[64], err[128] = "", want[16];
    snprintf(spec, sizeof spec, "udp-opentrack:port=%d", port);
    snprintf(want, sizeof want, "%d", port);
    assert_null(xrd_pose_source_create(spec, err, sizeof err));
    assert_non_null(strstr(err, want));
    close(holder);
}

static void seq_increments(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "");
    send_ypr(port, 1, 0, 0, 1);
    xrd_pose a, b;
    assert_true(wait_pose(s, 1, &a));
    send_ypr(port, 2, 0, 0, 2);
    assert_true(wait_pose(s, a.seq + 1, &b));
    assert_true(b.seq > a.seq);
    xrd_pose_source_destroy(s);
}

static void null_backend_identity(void **state)
{
    (void)state;
    char err[64];
    xrd_pose_source *s = xrd_pose_source_create("null", err, sizeof err);
    assert_non_null(s);
    xrd_pose p;
    assert_int_equal(s->latest(s, &p), 0);
    assert_float_equal(p.q.w, 1.0f, TOL);
    assert_float_equal(p.q.x, 0.0f, TOL);
    assert_float_equal(p.q.y, 0.0f, TOL);
    assert_float_equal(p.q.z, 0.0f, TOL);
    assert_int_equal(xrd_pose_udp_dropped(s), 0);
    xrd_pose_source_destroy(s);
}

static void unknown_backend(void **state)
{
    (void)state;
    char err[128] = "";
    assert_null(xrd_pose_source_create("bogus", err, sizeof err));
    assert_non_null(strstr(err, "bogus"));
    assert_null(xrd_pose_source_create("bogus:port=1", err, sizeof err));
    assert_non_null(strstr(err, "bogus"));
}

static void bad_args_rejected(void **state)
{
    (void)state;
    char err[128];
    const char *bad[] = {
        "udp-opentrack:port=0", "udp-opentrack:port=70000", "udp-opentrack:port=abc",
        "udp-opentrack:yaw=2", "udp-opentrack:pitch=", "udp-opentrack:bogus=1",
        "udp-opentrack:port",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        err[0] = '\0';
        assert_null(xrd_pose_source_create(bad[i], err, sizeof err));
        assert_true(err[0] != '\0');
    }
}

static void destroy_accepts_null(void **state)
{
    (void)state;
    xrd_pose_source_destroy(NULL);
    assert_int_equal(xrd_pose_udp_dropped(NULL), 0);
}

static void port_reusable_after_destroy(void **state)
{
    (void)state;
    int port = free_port();
    xrd_pose_source *s = make_udp(port, "");
    xrd_pose_source_destroy(s);
    s = make_udp(port, "");
    xrd_pose_source_destroy(s);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(no_data_returns_1),
        cmocka_unit_test(packet_updates_pose),
        cmocka_unit_test(nwu_pitch_maps_up),
        cmocka_unit_test(nwu_roll_maps),
        cmocka_unit_test(sign_option_applies),
        cmocka_unit_test(pitch_and_roll_sign_options_apply),
        cmocka_unit_test(bad_size_counted),
        cmocka_unit_test(frame_backwards_accepted),
        cmocka_unit_test(port_in_use_fails),
        cmocka_unit_test(seq_increments),
        cmocka_unit_test(null_backend_identity),
        cmocka_unit_test(unknown_backend),
        cmocka_unit_test(bad_args_rejected),
        cmocka_unit_test(destroy_accepts_null),
        cmocka_unit_test(port_reusable_after_destroy),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
