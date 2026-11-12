#ifndef XRD_MATH_QUAT_H
#define XRD_MATH_QUAT_H

/* Unit quaternion, world-from-head, GL axes (+x right, +y up, -z forward). */
typedef struct { float w, x, y, z; } xrd_quat;

static const xrd_quat XRD_QUAT_IDENTITY = {1, 0, 0, 0};

xrd_quat xrd_quat_mul(xrd_quat a, xrd_quat b);
xrd_quat xrd_quat_conj(xrd_quat q);
/* Identity if length is 0 or non-finite. */
xrd_quat xrd_quat_normalize(xrd_quat q);
/* Ry(yaw) * Rx(pitch) * Rz(roll), right-handed, angles in degrees. */
xrd_quat xrd_quat_from_ypr_deg(double yaw, double pitch, double roll);
/* Rotation about +y that keeps q's heading. */
xrd_quat xrd_quat_yaw_only(xrd_quat q);
void xrd_quat_rotate(xrd_quat q, const float v[3], float out[3]);
/* Column-major, for glUniformMatrix3fv. */
void xrd_quat_to_mat3(xrd_quat q, float m[9]);

#endif
