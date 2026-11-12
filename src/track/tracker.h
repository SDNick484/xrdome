#ifndef XRD_TRACK_TRACKER_H
#define XRD_TRACK_TRACKER_H

#include <stdbool.h>
#include <stdint.h>

#include "math/quat.h"
#include "pose/pose.h"

/* Pose newer than this many ns behind `now` is fresh; older is stale. */
#define XRD_STALE_NS 250000000ull

/* Turns absolute head poses from a pose source into a view rotation
 * relative to a reference. Does not own the source. */
typedef struct xrd_tracker xrd_tracker;

xrd_tracker *xrd_tracker_new(xrd_pose_source *src);
void xrd_tracker_free(xrd_tracker *t);

/* q_view = conj(q_ref) * q_raw. Identity until a pose has arrived; the first
 * pose triggers a full recenter. While the newest pose is stale, the last
 * view is held. */
xrd_quat xrd_tracker_view(xrd_tracker *t, uint64_t now_ns);

/* full: q_ref = q_raw. yaw-only: q_ref = q_ref * yaw_only(conj(q_ref) * q_raw).
 * Uses the pose most recently seen by xrd_tracker_view; no-op before any. */
void xrd_tracker_recenter(xrd_tracker *t, bool yaw_only);

/* Staleness as of the last xrd_tracker_view call. */
bool xrd_tracker_is_stale(const xrd_tracker *t);
/* t_mono_ns of the pose last used; 0 before any pose. */
uint64_t xrd_tracker_last_pose_ns(const xrd_tracker *t);

/* Warning sink ("no pose data", "pose stale", "pose resumed"); default
 * writes the line to stderr. NULL fn restores the default. */
void xrd_tracker_set_log(xrd_tracker *t,
                         void (*fn)(const char *msg, void *user), void *user);

#endif
