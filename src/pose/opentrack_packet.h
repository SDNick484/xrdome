#ifndef XRD_POSE_OPENTRACK_PACKET_H
#define XRD_POSE_OPENTRACK_PACKET_H

#include <stddef.h>
#include <stdint.h>

/* Little-endian, packed: 6 IEEE doubles (x, y, z, yaw, pitch, roll) then a
 * uint32 frame number. */
#define XRD_OT_PACKET_SIZE 52

/* out = x,y,z,yaw,pitch,roll. frame may be NULL. Returns 0, or -1 if
 * len != 52 or any double is non-finite (out and frame untouched). */
int xrd_opentrack_parse(const uint8_t *buf, size_t len, double out[6], uint32_t *frame);

#endif
