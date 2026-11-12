#include "pose/pose_internal.h"

#include <stdlib.h>

#include "util/timing.h"

typedef struct {
    xrd_pose_source base; /* must be first */
} null_backend;

static int null_open(xrd_pose_source *s, const char *args)
{
    (void)s;
    (void)args;
    return 0;
}

static int null_latest(xrd_pose_source *s, xrd_pose *out)
{
    (void)s;
    out->q = XRD_QUAT_IDENTITY;
    out->t_mono_ns = xrd_now_ns();
    out->seq = 0;
    return 0;
}

static void null_close(xrd_pose_source *s)
{
    (void)s;
}

xrd_pose_source *xrd_pose_null_alloc(void)
{
    null_backend *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->base.name = "null";
    b->base.open = null_open;
    b->base.latest = null_latest;
    b->base.close = null_close;
    return &b->base;
}
