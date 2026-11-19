#ifndef XRD_VITURE_PACKET_H
#define XRD_VITURE_PACKET_H

#include <stdint.h>

/* One OpenTrack UDP packet: six little-endian IEEE doubles (x, y, z, yaw,
 * pitch, roll) and a little-endian uint32 frame number. */
#define VITURE_OT_PACKET_SIZE 52

/* Position is sent as zero: the glasses give orientation only. */
void viture_ot_encode(const double ypr[3], uint32_t frame, uint8_t out[VITURE_OT_PACKET_SIZE]);

#endif
