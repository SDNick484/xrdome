#ifndef XRD_POSE_POSE_INTERNAL_H
#define XRD_POSE_POSE_INTERNAL_H

#include "pose/pose.h"

/* Backend constructors. Each returns a heap-allocated struct whose first
 * member is the xrd_pose_source, not yet opened. Freed by
 * xrd_pose_source_destroy via free(). */
xrd_pose_source *xrd_pose_null_alloc(void);
xrd_pose_source *xrd_pose_udp_alloc(void);

/* Message describing the last failed open() ("" if none). */
const char *xrd_pose_udp_open_error(const xrd_pose_source *s);

#endif
