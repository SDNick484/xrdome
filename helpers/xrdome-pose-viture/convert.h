#ifndef XRD_VITURE_CONVERT_H
#define XRD_VITURE_CONVERT_H

/* VITURE's pose callback delivers seven floats: roll, pitch, yaw, then the
 * quaternion w, x, y, z, in a North-West-Up frame (x north, y west, z up).
 * xrdome's OpenTrack reader takes angles in degrees with
 * q = Rz_up(yaw) * Ry_west(pitch) * Rx_north(roll) in that same frame.
 *
 * The angles are rebuilt from the quaternion, not copied from the callback's
 * own roll, pitch and yaw: the header does not say what unit or order those
 * are in, and a quaternion has one meaning. */

/* q = w, x, y, z (need not be unit length). ypr = yaw, pitch, roll in
 * degrees: yaw and roll in (-180, 180], pitch in [-90, 90]. Returns 0, or -1
 * (ypr untouched) when q has a non-finite part or no length. */
int viture_quat_to_ypr(const float q[4], double ypr[3]);

/* The same from the callback's seven floats. */
int viture_pose_to_ypr(const float data[7], double ypr[3]);

#endif
