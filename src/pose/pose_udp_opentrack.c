#define _POSIX_C_SOURCE 200809L /* sockets, pthread, strtol */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "pose/opentrack_packet.h"
#include "pose/pose_internal.h"
#include "util/timing.h"

#define DEFAULT_PORT 4242

/* Latest pose, published through a seqlock.
 *
 * Single writer assumption: only the receive thread calls publish(); two
 * concurrent writers would corrupt the sequence. Readers are wait-free for
 * the writer and retry while the sequence is odd (write in progress) or
 * changed across the read.
 *
 * Every field is a C11 atomic with the default seq_cst ordering. That is
 * stronger than a classic seqlock needs, but it keeps the code free of
 * standalone fences (which ThreadSanitizer does not model) and of formal
 * data races, and at ~365 packets/s the cost is irrelevant. Floats are
 * stored as their bit patterns. */
typedef struct {
    atomic_uint_least32_t seq;   /* odd while a write is in progress */
    atomic_uint_least32_t q[4];  /* w, x, y, z as float bits */
    atomic_uint_least64_t t_ns;
    atomic_uint_least32_t count; /* accepted packets; 0 = no sample yet */
} seqlock_pose;

typedef struct {
    xrd_pose_source base; /* must be first */
    int fd;
    pthread_t thread;
    bool thread_started;
    atomic_bool stop;
    float sign[3];               /* yaw, pitch, roll multipliers */
    seqlock_pose pub;
    atomic_uint_least32_t dropped;
    char errmsg[160];
} udp_backend;

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static float u2f(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

static void publish(seqlock_pose *p, xrd_quat q, uint64_t t_ns)
{
    uint32_t s = atomic_load(&p->seq);
    atomic_store(&p->seq, s + 1); /* odd: write in progress */
    atomic_store(&p->q[0], f2u(q.w));
    atomic_store(&p->q[1], f2u(q.x));
    atomic_store(&p->q[2], f2u(q.y));
    atomic_store(&p->q[3], f2u(q.z));
    atomic_store(&p->t_ns, t_ns);
    atomic_store(&p->count, atomic_load(&p->count) + 1);
    atomic_store(&p->seq, s + 2); /* even: consistent */
}

static int udp_latest(xrd_pose_source *src, xrd_pose *out)
{
    udp_backend *b = (udp_backend *)src;
    for (;;) {
        uint32_t s1 = atomic_load(&b->pub.seq);
        if (s1 & 1u)
            continue;
        xrd_pose p;
        p.q.w = u2f((uint32_t)atomic_load(&b->pub.q[0]));
        p.q.x = u2f((uint32_t)atomic_load(&b->pub.q[1]));
        p.q.y = u2f((uint32_t)atomic_load(&b->pub.q[2]));
        p.q.z = u2f((uint32_t)atomic_load(&b->pub.q[3]));
        p.t_mono_ns = atomic_load(&b->pub.t_ns);
        p.seq = (uint32_t)atomic_load(&b->pub.count);
        if (atomic_load(&b->pub.seq) != s1)
            continue;
        if (p.seq == 0)
            return 1;
        *out = p;
        return 0;
    }
}

/* Driver NWU Euler angles -> GL-axes quaternion. q = Rz_up(yaw) *
 * Ry_left(pitch) * Rx_forward(roll) in NWU, which in GL axes (up=+y,
 * left=-x, forward=-z) is from_ypr_deg(yaw, -pitch, -roll). */
static xrd_quat map_angles(const udp_backend *b, const double a[6])
{
    return xrd_quat_normalize(xrd_quat_from_ypr_deg(
        b->sign[0] * a[3], -b->sign[1] * a[4], -b->sign[2] * a[5]));
}

static void *recv_thread(void *arg)
{
    udp_backend *b = arg;
    uint8_t buf[256];
    while (!atomic_load(&b->stop)) {
        ssize_t n = recv(b->fd, buf, sizeof buf, 0);
        if (n < 0)
            continue; /* timeout (re-check stop) or EINTR */
        uint64_t t = xrd_now_ns();
        double a[6];
        uint32_t frame;
        /* Frame numbers are not used for ordering: a restarted driver may
         * legitimately go backwards, and UDP on loopback does not reorder. */
        if (xrd_opentrack_parse(buf, (size_t)n, a, &frame) != 0) {
            atomic_fetch_add(&b->dropped, 1);
            continue;
        }
        publish(&b->pub, map_angles(b, a), t);
    }
    return NULL;
}

static int parse_sign(const char *val, float *out)
{
    if (strcmp(val, "1") == 0)
        *out = 1.0f;
    else if (strcmp(val, "-1") == 0)
        *out = -1.0f;
    else
        return -1;
    return 0;
}

static int parse_args(udp_backend *b, const char *args, int *port)
{
    char copy[128];
    if (strlen(args) >= sizeof copy) {
        snprintf(b->errmsg, sizeof b->errmsg, "udp-opentrack: argument string too long");
        return -1;
    }
    strcpy(copy, args);

    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) {
            snprintf(b->errmsg, sizeof b->errmsg,
                     "udp-opentrack: expected key=val, got '%.60s'", tok);
            return -1;
        }
        *eq = '\0';
        const char *key = tok, *val = eq + 1;
        if (strcmp(key, "port") == 0) {
            char *end;
            errno = 0;
            long v = strtol(val, &end, 10);
            if (errno || *val == '\0' || *end != '\0' || v < 1 || v > 65535) {
                snprintf(b->errmsg, sizeof b->errmsg,
                         "udp-opentrack: invalid port '%.40s'", val);
                return -1;
            }
            *port = (int)v;
        } else {
            int idx = strcmp(key, "yaw") == 0 ? 0
                    : strcmp(key, "pitch") == 0 ? 1
                    : strcmp(key, "roll") == 0 ? 2 : -1;
            if (idx < 0) {
                snprintf(b->errmsg, sizeof b->errmsg,
                         "udp-opentrack: unknown key '%.40s'", key);
                return -1;
            }
            if (parse_sign(val, &b->sign[idx]) != 0) {
                snprintf(b->errmsg, sizeof b->errmsg,
                         "udp-opentrack: %s must be 1 or -1, got '%.40s'", key, val);
                return -1;
            }
        }
    }
    return 0;
}

static int udp_open(xrd_pose_source *src, const char *args)
{
    udp_backend *b = (udp_backend *)src;
    int port = DEFAULT_PORT;
    b->errmsg[0] = '\0';
    b->fd = -1;
    atomic_store(&b->stop, false);

    if (parse_args(b, args ? args : "", &port) != 0)
        return -1;

    b->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (b->fd < 0) {
        snprintf(b->errmsg, sizeof b->errmsg,
                 "udp-opentrack: socket() failed: %s", strerror(errno));
        return -1;
    }

    /* Deliberately no SO_REUSEADDR/SO_REUSEPORT: a second binder must fail. */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(b->fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        snprintf(b->errmsg, sizeof b->errmsg,
                 "udp-opentrack: cannot bind 127.0.0.1:%d: %s", port, strerror(errno));
        goto fail;
    }

    /* Lets close() join the thread promptly. */
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    if (setsockopt(b->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) != 0) {
        snprintf(b->errmsg, sizeof b->errmsg,
                 "udp-opentrack: SO_RCVTIMEO failed: %s", strerror(errno));
        goto fail;
    }

    int rc = pthread_create(&b->thread, NULL, recv_thread, b);
    if (rc != 0) {
        snprintf(b->errmsg, sizeof b->errmsg,
                 "udp-opentrack: pthread_create failed: %s", strerror(rc));
        goto fail;
    }
    b->thread_started = true;
    return 0;

fail:
    close(b->fd);
    b->fd = -1;
    return -1;
}

static void udp_close(xrd_pose_source *src)
{
    udp_backend *b = (udp_backend *)src;
    if (b->thread_started) {
        atomic_store(&b->stop, true);
        pthread_join(b->thread, NULL);
        b->thread_started = false;
    }
    if (b->fd >= 0) {
        close(b->fd);
        b->fd = -1;
    }
}

xrd_pose_source *xrd_pose_udp_alloc(void)
{
    udp_backend *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->base.name = "udp-opentrack";
    b->base.open = udp_open;
    b->base.latest = udp_latest;
    b->base.close = udp_close;
    b->fd = -1;
    b->sign[0] = b->sign[1] = b->sign[2] = 1.0f;
    return &b->base;
}

const char *xrd_pose_udp_open_error(const xrd_pose_source *s)
{
    return ((const udp_backend *)s)->errmsg;
}

uint32_t xrd_pose_udp_dropped(const xrd_pose_source *s)
{
    if (!s || strcmp(s->name, "udp-opentrack") != 0)
        return 0;
    return (uint32_t)atomic_load(&((const udp_backend *)s)->dropped);
}
