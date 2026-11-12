#include "track/tracker.h"

#include <stdio.h>
#include <stdlib.h>

struct xrd_tracker {
    xrd_pose_source *src;
    bool have_pose;       /* a pose has ever arrived */
    xrd_quat q_ref;
    xrd_quat q_raw;       /* newest pose seen by view() */
    xrd_quat last_view;
    uint64_t last_pose_ns;
    bool stale;
    bool nodata_logged;
    void (*log)(const char *msg, void *user);
    void *log_user;
};

static void default_log(const char *msg, void *user)
{
    (void)user;
    fprintf(stderr, "xrdome: %s\n", msg);
}

static void tlog(xrd_tracker *t, const char *msg)
{
    t->log(msg, t->log_user);
}

static xrd_quat relative(const xrd_tracker *t)
{
    return xrd_quat_normalize(xrd_quat_mul(xrd_quat_conj(t->q_ref), t->q_raw));
}

xrd_tracker *xrd_tracker_new(xrd_pose_source *src)
{
    xrd_tracker *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;
    t->src = src;
    t->q_ref = XRD_QUAT_IDENTITY;
    t->q_raw = XRD_QUAT_IDENTITY;
    t->last_view = XRD_QUAT_IDENTITY;
    t->log = default_log;
    return t;
}

void xrd_tracker_free(xrd_tracker *t)
{
    free(t);
}

void xrd_tracker_set_log(xrd_tracker *t,
                         void (*fn)(const char *msg, void *user), void *user)
{
    t->log = fn ? fn : default_log;
    t->log_user = fn ? user : NULL;
}

xrd_quat xrd_tracker_view(xrd_tracker *t, uint64_t now_ns)
{
    xrd_pose p;
    if (!t->src || t->src->latest(t->src, &p) != 0) {
        if (!t->have_pose && !t->nodata_logged) {
            t->nodata_logged = true;
            tlog(t, "no pose data");
        }
        return t->last_view;
    }

    bool first = !t->have_pose;
    t->have_pose = true;
    t->q_raw = xrd_quat_normalize(p.q);
    t->last_pose_ns = p.t_mono_ns;
    if (first)
        t->q_ref = t->q_raw;

    bool stale = now_ns > p.t_mono_ns && now_ns - p.t_mono_ns > XRD_STALE_NS;
    if (stale != t->stale) {
        t->stale = stale;
        tlog(t, stale ? "pose stale" : "pose resumed");
    }
    if (!stale)
        t->last_view = relative(t);
    return t->last_view;
}

void xrd_tracker_recenter(xrd_tracker *t, bool yaw_only)
{
    if (!t->have_pose)
        return;
    if (yaw_only) {
        xrd_quat yaw = xrd_quat_yaw_only(relative(t));
        t->q_ref = xrd_quat_normalize(xrd_quat_mul(t->q_ref, yaw));
    } else {
        t->q_ref = t->q_raw;
    }
    t->last_view = relative(t);
}

bool xrd_tracker_is_stale(const xrd_tracker *t)
{
    return t->stale;
}

uint64_t xrd_tracker_last_pose_ns(const xrd_tracker *t)
{
    return t->last_pose_ns;
}
