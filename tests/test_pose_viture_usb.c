#define _POSIX_C_SOURCE 200809L /* pthread */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "pose/pose.h"
#include "pose/pose_internal.h"
#include "pose/viture_proto.h"
#include "track/tracker.h"
#include "util/timing.h"

#define TOL 1e-4
#define DEADLINE_NS 500000000ull /* 500 ms */
#define RING 32
#define MAX_WRITES 16

/* One fake endpoint pair: a ring of reports to read, the reports written,
 * and, for the MCU, an automatic reply to each command. Shared with the
 * backend's reader thread, so everything goes through the mutex. */
typedef struct {
    pthread_mutex_t mu;
    uint8_t ring[RING][64];
    int ring_len[RING];
    int head, tail; /* read at head, written at tail */
    bool fail;      /* read returns -1 */
    uint8_t writes[MAX_WRITES][64];
    int nwrites;
    bool auto_reply;
    uint16_t reply_id;
    uint8_t reply_status;
} fake_ep;

static void fake_init(fake_ep *f)
{
    memset(f, 0, sizeof *f);
    pthread_mutex_init(&f->mu, NULL);
}

static void fake_destroy(fake_ep *f)
{
    pthread_mutex_destroy(&f->mu);
}

/* Caller holds the mutex. */
static void push_locked(fake_ep *f, const uint8_t *buf, int len)
{
    if ((f->tail + 1) % RING == f->head)
        fail_msg("fake ring full");
    memcpy(f->ring[f->tail], buf, (size_t)len);
    f->ring_len[f->tail] = len;
    f->tail = (f->tail + 1) % RING;
}

static void fake_push(fake_ep *f, const uint8_t *buf, int len)
{
    pthread_mutex_lock(&f->mu);
    push_locked(f, buf, len);
    pthread_mutex_unlock(&f->mu);
}

static void fake_set_fail(fake_ep *f, bool on)
{
    pthread_mutex_lock(&f->mu);
    f->fail = on;
    pthread_mutex_unlock(&f->mu);
}

static int fake_read(void *ctx, uint8_t *buf, size_t len, int timeout_ms)
{
    fake_ep *f = ctx;
    pthread_mutex_lock(&f->mu);
    if (f->fail) {
        pthread_mutex_unlock(&f->mu);
        return -1;
    }
    if (f->head != f->tail) {
        int n = f->ring_len[f->head];
        if ((size_t)n > len)
            n = (int)len;
        memcpy(buf, f->ring[f->head], (size_t)n);
        f->head = (f->head + 1) % RING;
        pthread_mutex_unlock(&f->mu);
        return n;
    }
    pthread_mutex_unlock(&f->mu);
    xrd_sleep_ns((uint64_t)(timeout_ms < 2 ? timeout_ms : 2) * 1000000ull);
    return 0;
}

static int fake_write(void *ctx, const uint8_t *buf, size_t len, int timeout_ms)
{
    (void)timeout_ms;
    fake_ep *f = ctx;
    pthread_mutex_lock(&f->mu);
    if (f->nwrites < MAX_WRITES && len <= 64)
        memcpy(f->writes[f->nwrites++], buf, len);
    if (f->auto_reply) {
        uint8_t reply[64];
        xrd_viture_cmd(f->reply_id, &f->reply_status, 1, reply);
        push_locked(f, reply, 64);
    }
    pthread_mutex_unlock(&f->mu);
    return (int)len;
}

static int fake_nwrites(fake_ep *f)
{
    pthread_mutex_lock(&f->mu);
    int n = f->nwrites;
    pthread_mutex_unlock(&f->mu);
    return n;
}

typedef struct {
    fake_ep imu, mcu;
} fakes;

static int setup(void **state)
{
    fakes *f = calloc(1, sizeof *f);
    assert_non_null(f);
    fake_init(&f->imu);
    fake_init(&f->mcu);
    f->mcu.auto_reply = true; /* reply id 0, status 0: success */
    *state = f;
    return 0;
}

static int teardown(void **state)
{
    fakes *f = *state;
    fake_destroy(&f->imu);
    fake_destroy(&f->mcu);
    free(f);
    return 0;
}

static xrd_pose_source *make_src(fakes *f)
{
    xrd_usb_io imu = {.read = fake_read, .write = NULL, .ctx = &f->imu};
    xrd_usb_io mcu = {.read = fake_read, .write = fake_write, .ctx = &f->mcu};
    xrd_pose_source *s = xrd_pose_viture_usb_alloc_io(&imu, &mcu);
    assert_non_null(s);
    return s;
}

static xrd_pose_source *open_src(fakes *f, const char *args)
{
    xrd_pose_source *s = make_src(f);
    if (s->open(s, args) != 0)
        fail_msg("open failed: %s", xrd_pose_viture_open_error(s));
    return s;
}

/* An IMU report with roll, pitch, yaw as big-endian floats. */
static void imu_packet(uint8_t out[64], float roll, float pitch, float yaw)
{
    const float a[3] = {roll, pitch, yaw};
    uint8_t d[12];
    for (int i = 0; i < 3; i++) {
        uint32_t u;
        memcpy(&u, &a[i], sizeof u);
        d[4 * i] = (uint8_t)(u >> 24);
        d[4 * i + 1] = (uint8_t)(u >> 16);
        d[4 * i + 2] = (uint8_t)(u >> 8);
        d[4 * i + 3] = (uint8_t)u;
    }
    assert_int_equal(xrd_viture_cmd(0, d, sizeof d, out), 64);
    out[1] = 0xFC;
    uint16_t crc = xrd_viture_crc(out + 4, 12 + 12 + 2);
    out[2] = (uint8_t)(crc & 0xFF);
    out[3] = (uint8_t)(crc >> 8);
}

static void push_ypr(fakes *f, float roll, float pitch, float yaw)
{
    uint8_t p[64];
    imu_packet(p, roll, pitch, yaw);
    fake_push(&f->imu, p, 64);
}

static bool wait_pose(xrd_pose_source *s, uint32_t min_seq, xrd_pose *out)
{
    uint64_t end = xrd_now_ns() + DEADLINE_NS;
    do {
        if (s->latest(s, out) == 0 && out->seq >= min_seq)
            return true;
        xrd_sleep_ns(500000);
    } while (xrd_now_ns() < end);
    return false;
}

static void expect_fwd(xrd_quat q, float x, float y, float z)
{
    const float fwd[3] = {0, 0, -1};
    float v[3];
    xrd_quat_rotate(q, fwd, v);
    assert_float_equal(v[0], x, TOL);
    assert_float_equal(v[1], y, TOL);
    assert_float_equal(v[2], z, TOL);
}

static void quiet_log(const char *msg, void *user)
{
    (void)msg;
    (void)user;
}

static void open_sends_imu_on(void **state)
{
    fakes *f = *state;
    xrd_pose_source *s = open_src(f, "");
    assert_string_equal(s->name, "viture-usb");
    assert_true(fake_nwrites(&f->mcu) >= 1);
    uint8_t want[64];
    const uint8_t on = 1;
    xrd_viture_cmd(XRD_VITURE_CMD_IMU, &on, 1, want);
    pthread_mutex_lock(&f->mcu.mu);
    assert_memory_equal(f->mcu.writes[0], want, 64);
    pthread_mutex_unlock(&f->mcu.mu);
    xrd_pose p;
    assert_int_equal(s->latest(s, &p), 1); /* no IMU packet yet */
    xrd_pose_source_destroy(s);
}

static void open_accepts_reply_with_cmd_id(void **state)
{
    fakes *f = *state;
    f->mcu.reply_id = XRD_VITURE_CMD_IMU; /* the decompiled SDK's reading */
    xrd_pose_source *s = open_src(f, "");
    xrd_pose_source_destroy(s);
}

static void open_fails_without_reply(void **state)
{
    fakes *f = *state;
    f->mcu.auto_reply = false;
    xrd_pose_source *s = make_src(f);
    uint64_t t0 = xrd_now_ns();
    assert_int_equal(s->open(s, ""), -1);
    uint64_t took = xrd_now_ns() - t0;
    assert_true(took >= 900000000ull);  /* waited about 1 s */
    assert_true(took < 3000000000ull);
    assert_non_null(strstr(xrd_pose_viture_open_error(s),
                           "viture-usb: the glasses did not start the IMU"));
    xrd_pose_source_destroy(s); /* close after a failed open is safe */
}

static void open_fails_on_error_status(void **state)
{
    fakes *f = *state;
    f->mcu.reply_status = 3;
    xrd_pose_source *s = make_src(f);
    assert_int_equal(s->open(s, ""), -1);
    assert_non_null(strstr(xrd_pose_viture_open_error(s), "did not start the IMU"));
    free(s);
}

static void open_skips_events_and_other_replies(void **state)
{
    fakes *f = *state;
    /* An event (id 0x40) and a reply to another command arrive first. */
    uint8_t ev[64];
    const uint8_t zero = 0;
    xrd_viture_cmd(0x40, &zero, 1, ev);
    fake_push(&f->mcu, ev, 64);
    xrd_viture_cmd(0x99, &zero, 1, ev);
    fake_push(&f->mcu, ev, 64);
    f->mcu.reply_status = 0;
    xrd_pose_source *s = open_src(f, "");
    xrd_pose_source_destroy(s);
}

static void latest_returns_newest(void **state)
{
    fakes *f = *state;
    xrd_pose_source *s = open_src(f, "");
    uint64_t before = xrd_now_ns();
    push_ypr(f, 0, 0, 10);
    push_ypr(f, 0, 0, 90);
    xrd_pose p;
    assert_true(wait_pose(s, 2, &p));
    assert_int_equal(p.seq, 2);
    expect_fwd(p.q, -1, 0, 0); /* yaw +90 NWU: looking left */
    assert_true(p.t_mono_ns >= before);
    assert_true(p.t_mono_ns <= xrd_now_ns());
    assert_int_equal(xrd_pose_viture_dropped(s), 0);
    xrd_pose_source_destroy(s);
}

static void sign_options_apply(void **state)
{
    fakes *f = *state;
    xrd_pose_source *s = open_src(f, "yaw=-1,pitch=-1,roll=1");
    push_ypr(f, 0, 0, 90);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    expect_fwd(p.q, 1, 0, 0);
    push_ypr(f, 0, 30, 0); /* NWU nose down, flipped: up */
    assert_true(wait_pose(s, 2, &p));
    expect_fwd(p.q, 0, 0.5f, -0.8660254f);
    xrd_pose_source_destroy(s);
}

static void bad_checksum_dropped(void **state)
{
    fakes *f = *state;
    xrd_pose_source *s = open_src(f, "");
    uint8_t bad[64];
    imu_packet(bad, 0, 0, 45);
    bad[0x12] ^= 0x01;
    fake_push(&f->imu, bad, 64);
    push_ypr(f, 0, 0, 90);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    assert_int_equal(p.seq, 1);
    expect_fwd(p.q, -1, 0, 0);
    assert_int_equal(xrd_pose_viture_dropped(s), 1);
    xrd_pose_source_destroy(s);
}

static void read_error_goes_stale(void **state)
{
    fakes *f = *state;
    xrd_pose_source *s = open_src(f, "");
    xrd_tracker *t = xrd_tracker_new(s);
    assert_non_null(t);
    xrd_tracker_set_log(t, quiet_log, NULL);

    push_ypr(f, 0, 0, 30);
    xrd_pose p;
    assert_true(wait_pose(s, 1, &p));
    xrd_tracker_view(t, p.t_mono_ns);
    assert_false(xrd_tracker_is_stale(t));

    fake_set_fail(&f->imu, true);
    xrd_sleep_ns(60000000); /* the reader meets a few errors */
    xrd_pose q;
    assert_int_equal(s->latest(s, &q), 0);
    assert_int_equal(q.seq, p.seq);
    assert_int_equal(q.t_mono_ns, p.t_mono_ns);
    assert_float_equal(q.q.w, p.q.w, 0);
    assert_float_equal(q.q.y, p.q.y, 0);

    xrd_tracker_view(t, p.t_mono_ns + XRD_STALE_NS + 1);
    assert_true(xrd_tracker_is_stale(t));

    /* The stream comes back: fresh again. */
    fake_set_fail(&f->imu, false);
    push_ypr(f, 0, 0, 31);
    assert_true(wait_pose(s, 2, &q));
    xrd_tracker_view(t, q.t_mono_ns);
    assert_false(xrd_tracker_is_stale(t));

    xrd_tracker_free(t);
    xrd_pose_source_destroy(s); /* joins promptly despite the errors */
}

static void close_sends_imu_off(void **state)
{
    fakes *f = *state;
    xrd_pose_source *s = open_src(f, "");
    int before = fake_nwrites(&f->mcu);
    xrd_pose_source_destroy(s);
    int n = fake_nwrites(&f->mcu);
    assert_int_equal(n, before + 1);
    uint8_t want[64];
    const uint8_t off = 0;
    xrd_viture_cmd(XRD_VITURE_CMD_IMU, &off, 1, want);
    assert_memory_equal(f->mcu.writes[n - 1], want, 64);
}

static void injected_rejects_fd_and_bad_args(void **state)
{
    fakes *f = *state;
    const char *bad[] = {"fd=3", "dev=/dev/null", "yaw=2", "pitch=", "bogus=1", "roll"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        xrd_pose_source *s = make_src(f);
        assert_int_equal(s->open(s, bad[i]), -1);
        assert_true(xrd_pose_viture_open_error(s)[0] != '\0');
        free(s);
    }
    assert_int_equal(fake_nwrites(&f->mcu), 0); /* nothing sent */
}

static void spec_strings(void **state)
{
    (void)state;
    char err[256] = "";
#ifdef __linux__
    assert_null(xrd_pose_source_create("viture-usb", err, sizeof err));
    assert_non_null(strstr(err, "fd=N or dev="));
    assert_null(xrd_pose_source_create("viture-usb:dev=/nonexistent/usb/001/002", err, sizeof err));
    assert_non_null(strstr(err, "cannot open"));
    assert_null(xrd_pose_source_create("viture-usb:fd=3,dev=/x", err, sizeof err));
    assert_non_null(strstr(err, "not both"));
    assert_null(xrd_pose_source_create("viture-usb:fd=-1", err, sizeof err));
    assert_non_null(strstr(err, "invalid fd"));
#else
    assert_null(xrd_pose_source_create("viture-usb:fd=3", err, sizeof err));
    assert_string_equal(err, "viture-usb: Linux and Android only");
    assert_null(xrd_pose_source_create("viture-usb:dev=/dev/bus/usb/001/002", err, sizeof err));
    assert_string_equal(err, "viture-usb: Linux and Android only");
#endif
}

static void dropped_other_backends(void **state)
{
    (void)state;
    char err[64];
    assert_int_equal(xrd_pose_viture_dropped(NULL), 0);
    xrd_pose_source *s = xrd_pose_source_create("null", err, sizeof err);
    assert_non_null(s);
    assert_int_equal(xrd_pose_viture_dropped(s), 0);
    xrd_pose_source_destroy(s);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(open_sends_imu_on, setup, teardown),
        cmocka_unit_test_setup_teardown(open_accepts_reply_with_cmd_id, setup, teardown),
        cmocka_unit_test_setup_teardown(open_fails_without_reply, setup, teardown),
        cmocka_unit_test_setup_teardown(open_fails_on_error_status, setup, teardown),
        cmocka_unit_test_setup_teardown(open_skips_events_and_other_replies, setup, teardown),
        cmocka_unit_test_setup_teardown(latest_returns_newest, setup, teardown),
        cmocka_unit_test_setup_teardown(sign_options_apply, setup, teardown),
        cmocka_unit_test_setup_teardown(bad_checksum_dropped, setup, teardown),
        cmocka_unit_test_setup_teardown(read_error_goes_stale, setup, teardown),
        cmocka_unit_test_setup_teardown(close_sends_imu_off, setup, teardown),
        cmocka_unit_test_setup_teardown(injected_rejects_fd_and_bad_args, setup, teardown),
        cmocka_unit_test(spec_strings),
        cmocka_unit_test(dropped_other_backends),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
