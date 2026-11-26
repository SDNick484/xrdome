#ifndef XRD_POSE_POSE_INTERNAL_H
#define XRD_POSE_POSE_INTERNAL_H

#include "pose/pose.h"
#include "pose/usb_io.h"

/* Backend constructors. Each returns a heap-allocated struct whose first
 * member is the xrd_pose_source, not yet opened. Freed by
 * xrd_pose_source_destroy via free(). */
xrd_pose_source *xrd_pose_null_alloc(void);
xrd_pose_source *xrd_pose_udp_alloc(void);
xrd_pose_source *xrd_pose_viture_usb_alloc(void);

/* Message describing the last failed open() ("" if none). */
const char *xrd_pose_udp_open_error(const xrd_pose_source *s);
const char *xrd_pose_viture_open_error(const xrd_pose_source *s);

/* viture-usb over caller-supplied endpoint pairs (the tests' fakes): open()
 * then takes no fd or dev, only yaw, pitch, roll, and runs on every platform.
 * The pairs are copied; their ctx must outlive the source. */
xrd_pose_source *xrd_pose_viture_usb_alloc_io(const xrd_usb_io *imu, const xrd_usb_io *mcu);

#endif
