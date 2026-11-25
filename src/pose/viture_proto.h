#ifndef XRD_POSE_VITURE_PROTO_H
#define XRD_POSE_VITURE_PROTO_H

/* The VITURE Pro XR's USB protocol ("Gen1"), framing and IMU packets, as
 * mgschwan/viture_virtual_display's viture_connection.c has it (MIT; that
 * file says it was taken from a decompiled VITURE SDK). See viture_proto.c
 * for the notice and docs/licence.md for the provenance. Pure functions: no
 * I/O here. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "math/quat.h"

/* Command: IMU stream on (data [1]) or off ([0]), on the MCU interface. */
#define XRD_VITURE_CMD_IMU 0x15
/* Interfaces and endpoints of the glasses (35ca:101d, 64-byte reports). */
#define XRD_VITURE_IF_IMU 0
#define XRD_VITURE_IF_MCU 1
#define XRD_VITURE_EP_IMU_IN 0x81
#define XRD_VITURE_EP_MCU_IN 0x82
#define XRD_VITURE_EP_MCU_OUT 0x02

/* Every report is this long. */
#define XRD_VITURE_REPORT 64
/* Message kinds: the second byte of a frame. */
#define XRD_VITURE_KIND_MCU 0xFE /* command, reply or event */
#define XRD_VITURE_KIND_IMU 0xFC /* IMU packet */

/* CRC-16-CCITT, polynomial 0x1021, initial value 0, no reflection, no final
 * xor (the CRC-16/XMODEM parameters). */
uint16_t xrd_viture_crc(const uint8_t *p, size_t n);

/* Builds a command frame in out (always 64 bytes, zero-padded): FF FE, CRC
 * (LE) over the length field and the `length` bytes after it, length (LE,
 * 12 + len), 8 zero bytes, cmd_id (LE) at 0x0E, 2 zero bytes, data from
 * 0x12. Returns 64, or 0 if len > 46 (more than a report holds). data may be
 * NULL when len is 0. */
size_t xrd_viture_cmd(uint16_t cmd_id, const uint8_t *data, uint16_t len, uint8_t out[64]);

/* A parsed frame. data points into the buffer given to xrd_viture_parse. */
typedef struct {
    uint8_t kind;    /* XRD_VITURE_KIND_MCU or XRD_VITURE_KIND_IMU */
    uint16_t cmd_id; /* header command id at 0x0E (0 on some replies) */
    uint32_t ts;     /* the device's 32-bit timestamp at offset 6 (LE) */
    uint16_t len;    /* data bytes: the length field minus 12 */
    const uint8_t *data;
} xrd_viture_msg;

/* false on a first byte other than FF, a second other than FE or FC, a
 * buffer shorter than the header (18 bytes), a length field below 12 or
 * reaching past the buffer, or a CRC mismatch. */
bool xrd_viture_parse(const uint8_t *buf, size_t len, xrd_viture_msg *out);

/* Roll, pitch and yaw in degrees from an IMU message: big-endian floats at
 * data 0, 4 and 8. false if m->len < 12. */
bool xrd_viture_imu_ypr(const xrd_viture_msg *m, float *roll, float *pitch, float *yaw);

/* NWU Euler angles (x north/forward, y west/left, z up; q = Rz(yaw) *
 * Ry(pitch) * Rx(roll), XRLinuxDriver's order) to xrdome's GL frame (+x
 * right, +y up, -z forward): from_ypr_deg(yaw, -pitch, -roll), normalized,
 * the mapping pose_udp_opentrack.c's map_angles documents. Positive yaw looks
 * left, positive pitch looks down, positive roll tilts the right ear down. */
xrd_quat xrd_viture_to_gl(float roll, float pitch, float yaw);

#endif
