/* viture-usb: head pose from the VITURE Pro XR's own USB protocol, no SDK.
 *
 * Spec strings: viture-usb:fd=N (Android: a usbfs descriptor the app opened
 * through UsbManager, interfaces 0 and 1 already claimed) or
 * viture-usb:dev=/dev/bus/usb/BBB/DDD (Linux: opened, detached and claimed
 * here), each with optional yaw, pitch, roll (1 or -1). Open sends IMU on to
 * the MCU interface and waits up to 1 s for its reply; a reader thread then
 * keeps the newest pose from the IMU interface. latest() never blocks.
 *
 * The protocol (pose/viture_proto.c) and the open/close sequence are ported
 * from viture_connection.c of mgschwan/viture_virtual_display,
 * https://github.com/mgschwan/viture_virtual_display (commit 6d50d39), which
 * says it reimplements the protocol from a decompiled VITURE SDK. See
 * docs/licence.md for the provenance, and for option B, which replaces this
 * code if VITURE asks. Nothing else in xrdome depends on this file.
 *
 * The protocol reading this file is ported from is under the MIT licence:
 *
 * MIT License
 *
 * Copyright (c) 2025 Michael Gschwandtner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _POSIX_C_SOURCE 200809L /* pthread, strtok_r, close */

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <unistd.h>

#include "pose/usbfs.h"
#endif

#include "pose/pose_internal.h"
#include "pose/usb_io.h"
#include "pose/viture_proto.h"
#include "util/timing.h"

#define NAME "viture-usb"
#define IMU_ON_WAIT_MS 1000   /* open: the IMU-on reply */
#define IMU_OFF_WAIT_MS 200   /* close: the IMU-off reply, best effort */
#define WRITE_TIMEOUT_MS 1000
#define IMU_READ_MS 100       /* the reader's poll; bounds close()'s join */
#define ERROR_BACKOFF_NS 20000000ull   /* after a failed IMU read */
#define MCU_DRAIN_EVERY_NS 100000000ull
#define MCU_DRAIN_MS 1

/* Latest pose through a seqlock, single writer (the reader thread); the same
 * scheme as pose_udp_opentrack.c, kept separate so this backend stays in its
 * own files. Floats are stored as their bit patterns. */
typedef struct {
    atomic_uint_least32_t seq;   /* odd while a write is in progress */
    atomic_uint_least32_t q[4];  /* w, x, y, z as float bits */
    atomic_uint_least64_t t_ns;
    atomic_uint_least32_t count; /* accepted packets; 0 = no sample yet */
} seqlock_pose;

typedef struct {
    xrd_pose_source base; /* must be first */
    xrd_usb_io imu;       /* IMU interface: reads only */
    xrd_usb_io mcu;       /* MCU interface: commands and replies */
    bool injected;        /* alloc_io (tests): the pairs came from the caller */
    int fd;               /* usbfs descriptor, -1 if none */
    bool own_fd;          /* dev= form: opened here, closed on close */
    bool claimed[2];      /* by interface number, dev= form only */
    pthread_t thread;
    bool thread_started;
    atomic_bool stop;
    float sign[3];        /* yaw, pitch, roll multipliers */
    seqlock_pose pub;
    atomic_uint_least32_t dropped;
    char errmsg[200];
} viture_backend;

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

static int viture_latest(xrd_pose_source *src, xrd_pose *out)
{
    viture_backend *b = (viture_backend *)src;
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

/* Sends cmd_id with one data byte on the MCU pair and waits up to wait_ms
 * for its reply: a FE frame whose header id is 0 or cmd_id (mgschwan's code
 * takes 0, and notes the decompiled code compares it with the request's id)
 * with at least one data byte, the status. Other frames on the way (events)
 * are skipped. 0 with *status set, -1 on a write or read error (errno), 1 if
 * no reply came in time. */
static int mcu_command(viture_backend *b, uint16_t cmd_id, uint8_t byte, int wait_ms,
                       uint8_t *status)
{
    uint8_t frame[XRD_VITURE_REPORT];
    if (xrd_viture_cmd(cmd_id, &byte, 1, frame) != sizeof frame || !b->mcu.write)
        return -1;
    if (b->mcu.write(b->mcu.ctx, frame, sizeof frame, WRITE_TIMEOUT_MS) != (int)sizeof frame)
        return -1;
    if (!b->mcu.read)
        return 1;
    uint64_t end = xrd_now_ns() + (uint64_t)wait_ms * 1000000ull;
    for (;;) {
        uint64_t now = xrd_now_ns();
        if (now >= end)
            return 1;
        int left_ms = (int)((end - now + 999999ull) / 1000000ull);
        uint8_t buf[XRD_VITURE_REPORT];
        int n = b->mcu.read(b->mcu.ctx, buf, sizeof buf, left_ms);
        if (n < 0)
            return -1;
        if (n == 0)
            continue;
        xrd_viture_msg m;
        if (!xrd_viture_parse(buf, (size_t)n, &m)) {
            atomic_fetch_add(&b->dropped, 1);
            continue;
        }
        if (m.kind == XRD_VITURE_KIND_MCU && (m.cmd_id == 0 || m.cmd_id == cmd_id) && m.len >= 1) {
            *status = m.data[0];
            return 0;
        }
    }
}

/* NWU Euler angles from the glasses, with the spec's signs, to GL axes.
 * Defaults are all +1: the angles are taken as NWU (XRLinuxDriver reads the
 * same glasses into that convention). mgschwan negates yaw, but he feeds the
 * angles to glRotatef on the scene, the head's inverse, which is where the
 * negation belongs; pitch and roll he passes unchanged, which on the scene
 * also reads as NWU. H5b on the glasses settles it; a wrong sign is fixed in
 * these defaults, not with a flag. */
static xrd_quat map_angles(const viture_backend *b, float roll, float pitch, float yaw)
{
    return xrd_viture_to_gl(b->sign[2] * roll, b->sign[1] * pitch, b->sign[0] * yaw);
}

/* The MCU interface also carries events nobody here uses; reading them now
 * and then keeps the device from queueing them up (mgschwan's driver reads
 * it all the time). Errors and timeouts are ignored. */
static void drain_mcu(viture_backend *b)
{
    if (!b->mcu.read)
        return;
    uint8_t buf[XRD_VITURE_REPORT];
    (void)b->mcu.read(b->mcu.ctx, buf, sizeof buf, MCU_DRAIN_MS);
}

static void *reader_thread(void *arg)
{
    viture_backend *b = arg;
    uint8_t buf[XRD_VITURE_REPORT];
    uint64_t next_drain = xrd_now_ns() + MCU_DRAIN_EVERY_NS;
    while (!atomic_load(&b->stop)) {
        int n = b->imu.read(b->imu.ctx, buf, sizeof buf, IMU_READ_MS);
        uint64_t t = xrd_now_ns();
        if (t >= next_drain) {
            drain_mcu(b);
            next_drain = t + MCU_DRAIN_EVERY_NS;
        }
        if (n < 0) {
            /* Stall, permission revoked, glasses unplugged: keep the last
             * pose (the tracker turns it stale) and try again shortly. */
            xrd_sleep_ns(ERROR_BACKOFF_NS);
            continue;
        }
        if (n == 0)
            continue; /* timeout: re-check stop */
        xrd_viture_msg m;
        if (!xrd_viture_parse(buf, (size_t)n, &m)) {
            atomic_fetch_add(&b->dropped, 1);
            continue;
        }
        if (m.kind != XRD_VITURE_KIND_IMU)
            continue;
        float roll, pitch, yaw;
        if (!xrd_viture_imu_ypr(&m, &roll, &pitch, &yaw) || !isfinite(roll) ||
            !isfinite(pitch) || !isfinite(yaw)) {
            atomic_fetch_add(&b->dropped, 1);
            continue;
        }
        publish(&b->pub, map_angles(b, roll, pitch, yaw), t);
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

/* fd=N, dev=PATH, yaw, pitch, roll. *fd_arg -1 and dev[0] 0 when absent. */
static int parse_args(viture_backend *b, const char *args, int *fd_arg, char *dev, size_t devlen)
{
    char copy[256];
    if (strlen(args) >= sizeof copy) {
        snprintf(b->errmsg, sizeof b->errmsg, NAME ": argument string too long");
        return -1;
    }
    strcpy(copy, args);

    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) {
            snprintf(b->errmsg, sizeof b->errmsg, NAME ": expected key=val, got '%.60s'", tok);
            return -1;
        }
        *eq = '\0';
        const char *key = tok, *val = eq + 1;
        if (strcmp(key, "fd") == 0) {
            char *end;
            errno = 0;
            long v = strtol(val, &end, 10);
            if (errno || *val == '\0' || *end != '\0' || v < 0 || v > 0x7FFFFFFF) {
                snprintf(b->errmsg, sizeof b->errmsg, NAME ": invalid fd '%.40s'", val);
                return -1;
            }
            *fd_arg = (int)v;
        } else if (strcmp(key, "dev") == 0) {
            if (*val == '\0' || strlen(val) >= devlen) {
                snprintf(b->errmsg, sizeof b->errmsg, NAME ": invalid dev '%.60s'", val);
                return -1;
            }
            strcpy(dev, val);
        } else {
            int idx = strcmp(key, "yaw") == 0 ? 0
                    : strcmp(key, "pitch") == 0 ? 1
                    : strcmp(key, "roll") == 0 ? 2 : -1;
            if (idx < 0) {
                snprintf(b->errmsg, sizeof b->errmsg, NAME ": unknown key '%.40s'", key);
                return -1;
            }
            if (parse_sign(val, &b->sign[idx]) != 0) {
                snprintf(b->errmsg, sizeof b->errmsg, NAME ": %s must be 1 or -1, got '%.40s'",
                         key, val);
                return -1;
            }
        }
    }
    return 0;
}

/* Gives back what open() took: the interfaces and descriptor of the dev=
 * form. The fd= form's descriptor and claims are the app's. */
static void release_transport(viture_backend *b)
{
#ifdef __linux__
    if (b->own_fd && b->fd >= 0) {
        for (int i = 1; i >= 0; i--) {
            if (b->claimed[i])
                xrd_usbfs_release(b->fd, i);
            b->claimed[i] = false;
        }
        close(b->fd);
    }
#endif
    b->fd = -1;
    b->own_fd = false;
    if (!b->injected) {
        memset(&b->imu, 0, sizeof b->imu);
        memset(&b->mcu, 0, sizeof b->mcu);
    }
}

#ifdef __linux__
static int open_usbfs(viture_backend *b, int fd_arg, const char *dev)
{
    if (fd_arg >= 0 && dev[0]) {
        snprintf(b->errmsg, sizeof b->errmsg, NAME ": give fd or dev, not both");
        return -1;
    }
    if (fd_arg < 0 && !dev[0]) {
        snprintf(b->errmsg, sizeof b->errmsg,
                 NAME ": needs fd=N or dev=/dev/bus/usb/BBB/DDD");
        return -1;
    }
    if (dev[0]) {
        b->fd = xrd_usbfs_open(dev);
        if (b->fd < 0) {
            snprintf(b->errmsg, sizeof b->errmsg, NAME ": cannot open %.80s: %s", dev,
                     strerror(errno));
            return -1;
        }
        b->own_fd = true;
        const int ifs[2] = {XRD_VITURE_IF_IMU, XRD_VITURE_IF_MCU};
        for (int i = 0; i < 2; i++) {
            if (xrd_usbfs_claim(b->fd, ifs[i]) != 0) {
                snprintf(b->errmsg, sizeof b->errmsg,
                         NAME ": cannot claim interface %d of %.60s: %s (is another driver, "
                         "such as XRLinuxDriver, using the glasses?)",
                         ifs[i], dev, strerror(errno));
                return -1;
            }
            b->claimed[ifs[i]] = true;
        }
    } else {
        b->fd = fd_arg;
    }
    if (xrd_usbfs_io(b->fd, XRD_VITURE_EP_IMU_IN, 0, &b->imu) != 0 ||
        xrd_usbfs_io(b->fd, XRD_VITURE_EP_MCU_IN, XRD_VITURE_EP_MCU_OUT, &b->mcu) != 0) {
        snprintf(b->errmsg, sizeof b->errmsg, NAME ": bad usbfs descriptor %d", b->fd);
        return -1;
    }
    return 0;
}
#endif

static int viture_open(xrd_pose_source *src, const char *args)
{
    viture_backend *b = (viture_backend *)src;
    b->errmsg[0] = '\0';
    atomic_store(&b->stop, false);

#ifndef __linux__
    if (!b->injected) {
        snprintf(b->errmsg, sizeof b->errmsg, NAME ": Linux and Android only");
        return -1;
    }
#endif

    int fd_arg = -1;
    char dev[160] = "";
    if (parse_args(b, args ? args : "", &fd_arg, dev, sizeof dev) != 0)
        return -1;

    if (b->injected) {
        if (fd_arg >= 0 || dev[0]) {
            snprintf(b->errmsg, sizeof b->errmsg, NAME ": fd and dev do not apply here");
            return -1;
        }
    } else {
#ifdef __linux__
        if (open_usbfs(b, fd_arg, dev) != 0)
            goto fail;
#endif
    }
    if (!b->imu.read || !b->mcu.write) {
        snprintf(b->errmsg, sizeof b->errmsg, NAME ": no IMU or MCU transport");
        goto fail;
    }

    uint8_t status = 0xFF;
    int rc = mcu_command(b, XRD_VITURE_CMD_IMU, 1, IMU_ON_WAIT_MS, &status);
    if (rc < 0) {
        snprintf(b->errmsg, sizeof b->errmsg,
                 NAME ": the glasses did not start the IMU (USB transfer failed: %s)",
                 strerror(errno));
        goto fail;
    }
    if (rc > 0) {
        snprintf(b->errmsg, sizeof b->errmsg,
                 NAME ": the glasses did not start the IMU (no reply within 1 s)");
        goto fail;
    }
    if (status != 0) {
        snprintf(b->errmsg, sizeof b->errmsg,
                 NAME ": the glasses did not start the IMU (status %u)", (unsigned)status);
        goto fail;
    }

    int prc = pthread_create(&b->thread, NULL, reader_thread, b);
    if (prc != 0) {
        snprintf(b->errmsg, sizeof b->errmsg, NAME ": pthread_create failed: %s",
                 strerror(prc));
        uint8_t ignored;
        (void)mcu_command(b, XRD_VITURE_CMD_IMU, 0, IMU_OFF_WAIT_MS, &ignored);
        goto fail;
    }
    b->thread_started = true;
    return 0;

fail:
    release_transport(b);
    return -1;
}

/* Safe after a failed open and twice in a row. */
static void viture_close(xrd_pose_source *src)
{
    viture_backend *b = (viture_backend *)src;
    if (b->thread_started) {
        /* The reader first (it also drains the MCU pair, which would race
         * the reply), then IMU off; its reply is waited for briefly and not
         * required. */
        atomic_store(&b->stop, true);
        pthread_join(b->thread, NULL);
        b->thread_started = false;
        uint8_t ignored;
        (void)mcu_command(b, XRD_VITURE_CMD_IMU, 0, IMU_OFF_WAIT_MS, &ignored);
    }
    release_transport(b);
}

static viture_backend *alloc_backend(void)
{
    viture_backend *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->base.name = NAME;
    b->base.open = viture_open;
    b->base.latest = viture_latest;
    b->base.close = viture_close;
    b->fd = -1;
    b->sign[0] = b->sign[1] = b->sign[2] = 1.0f;
    return b;
}

xrd_pose_source *xrd_pose_viture_usb_alloc(void)
{
    viture_backend *b = alloc_backend();
    return b ? &b->base : NULL;
}

xrd_pose_source *xrd_pose_viture_usb_alloc_io(const xrd_usb_io *imu, const xrd_usb_io *mcu)
{
    if (!imu || !mcu)
        return NULL;
    viture_backend *b = alloc_backend();
    if (!b)
        return NULL;
    b->imu = *imu;
    b->mcu = *mcu;
    b->injected = true;
    return &b->base;
}

const char *xrd_pose_viture_open_error(const xrd_pose_source *s)
{
    return ((const viture_backend *)s)->errmsg;
}

uint32_t xrd_pose_viture_dropped(const xrd_pose_source *s)
{
    if (!s || strcmp(s->name, NAME) != 0)
        return 0;
    return (uint32_t)atomic_load(&((const viture_backend *)s)->dropped);
}
