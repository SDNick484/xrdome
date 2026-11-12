#include "pose/opentrack_packet.h"

#include <math.h>
#include <string.h>

/* Byte-wise little-endian load: independent of host endianness and alignment. */
static uint64_t load_le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

int xrd_opentrack_parse(const uint8_t *buf, size_t len, double out[6], uint32_t *frame)
{
    if (!buf || len != XRD_OT_PACKET_SIZE)
        return -1;

    double d[6];
    for (int i = 0; i < 6; i++) {
        uint64_t bits = load_le64(buf + 8 * i);
        memcpy(&d[i], &bits, sizeof d[i]);
        if (!isfinite(d[i]))
            return -1;
    }
    memcpy(out, d, sizeof d);
    if (frame) {
        const uint8_t *p = buf + 48;
        *frame = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
                 (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
    return 0;
}
