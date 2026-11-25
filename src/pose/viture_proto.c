/* The VITURE Pro XR's USB protocol: framing, CRC and IMU packets.
 *
 * Ported from viture_connection.c (and makeFloat in v4l2_gl.c) of
 * mgschwan/viture_virtual_display, https://github.com/mgschwan/viture_virtual_display
 * (commit 6d50d39), which says it reimplements the protocol from a
 * decompiled VITURE SDK. Rewritten for xrdome: explicit little- and
 * big-endian access instead of pointer casts, a bitwise CRC, a parser that
 * rejects instead of warning. See docs/licence.md for the provenance, and
 * for option B, which replaces this code if VITURE asks.
 *
 * The protocol reading this file is ported from is under the MIT licence:
 *
 * MIT License
 *
 * Copyright (c) 2025 Michael Gschwandtner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pose/viture_proto.h"

#include <string.h>

/* Frame layout (offsets in bytes). */
#define OFF_CRC 2
#define OFF_LEN 4
#define OFF_TS 6
#define OFF_CMD 0x0E
#define OFF_DATA 0x12
#define HDR_LEN 12 /* the length field's minimum: 8 + 2 (cmd id) + 2 */
#define MAX_DATA (XRD_VITURE_REPORT - OFF_DATA) /* 46 */

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static float get_be_float(const uint8_t *p)
{
    uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
                 (uint32_t)p[3];
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

uint16_t xrd_viture_crc(const uint8_t *p, size_t n)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

size_t xrd_viture_cmd(uint16_t cmd_id, const uint8_t *data, uint16_t len, uint8_t out[64])
{
    if (len > MAX_DATA || (len > 0 && !data))
        return 0;
    memset(out, 0, XRD_VITURE_REPORT);
    out[0] = 0xFF;
    out[1] = XRD_VITURE_KIND_MCU;
    uint16_t field = (uint16_t)(HDR_LEN + len);
    put_le16(out + OFF_LEN, field);
    put_le16(out + OFF_CMD, cmd_id);
    if (len > 0)
        memcpy(out + OFF_DATA, data, len);
    /* Over the length field and the `field` bytes after it. */
    put_le16(out + OFF_CRC, xrd_viture_crc(out + OFF_LEN, (size_t)field + 2));
    return XRD_VITURE_REPORT;
}

bool xrd_viture_parse(const uint8_t *buf, size_t len, xrd_viture_msg *out)
{
    if (!buf || len < OFF_DATA || buf[0] != 0xFF)
        return false;
    if (buf[1] != XRD_VITURE_KIND_MCU && buf[1] != XRD_VITURE_KIND_IMU)
        return false;
    uint16_t field = get_le16(buf + OFF_LEN);
    /* The frame runs from the length field to OFF_LEN + 2 + field. */
    if (field < HDR_LEN || (size_t)OFF_LEN + 2 + field > len)
        return false;
    if (xrd_viture_crc(buf + OFF_LEN, (size_t)field + 2) != get_le16(buf + OFF_CRC))
        return false;
    if (out) {
        out->kind = buf[1];
        out->cmd_id = get_le16(buf + OFF_CMD);
        out->ts = get_le32(buf + OFF_TS);
        out->len = (uint16_t)(field - HDR_LEN);
        out->data = buf + OFF_DATA;
    }
    return true;
}

bool xrd_viture_imu_ypr(const xrd_viture_msg *m, float *roll, float *pitch, float *yaw)
{
    if (!m || !m->data || m->len < 12)
        return false;
    *roll = get_be_float(m->data);
    *pitch = get_be_float(m->data + 4);
    *yaw = get_be_float(m->data + 8);
    return true;
}

xrd_quat xrd_viture_to_gl(float roll, float pitch, float yaw)
{
    /* NWU forward (+x) is GL -z, left (+y) is GL -x, up (+z) is GL +y: a
     * rotation about NWU up is one about GL +y (same sign), about NWU left
     * one about GL -x, about NWU forward one about GL -z. */
    return xrd_quat_normalize(xrd_quat_from_ypr_deg(yaw, -(double)pitch, -(double)roll));
}
