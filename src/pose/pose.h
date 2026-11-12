#ifndef XRD_POSE_POSE_H
#define XRD_POSE_POSE_H

#include <stddef.h>
#include <stdint.h>

#include "math/quat.h"

typedef struct {
    xrd_quat q;
    uint64_t t_mono_ns;   /* CLOCK_MONOTONIC at receipt */
    uint32_t seq;
} xrd_pose;

typedef struct xrd_pose_source {
    const char *name;
    int  (*open)(struct xrd_pose_source *, const char *args);
    int  (*latest)(struct xrd_pose_source *, xrd_pose *out);  /* non-blocking */
    void (*close)(struct xrd_pose_source *);
} xrd_pose_source;

/* spec = "name" or "name:key=val,key=val". Names: "null", "udp-opentrack".
 * udp-opentrack keys: port (default 4242), yaw, pitch, roll (each 1 or -1,
 * default 1). Allocates the backend, calls open(); returns NULL with err set
 * on failure. */
xrd_pose_source *xrd_pose_source_create(const char *spec, char *err, size_t errlen);

/* Calls close() and frees. NULL is accepted. */
void xrd_pose_source_destroy(xrd_pose_source *s);

/* latest(): 0 and *out filled once at least one sample has arrived, 1 if none. */

/* Datagrams rejected by the parser so far; 0 for other backends or NULL. */
uint32_t xrd_pose_udp_dropped(const xrd_pose_source *s);

#endif
