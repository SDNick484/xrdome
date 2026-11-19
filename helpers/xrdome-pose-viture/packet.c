#include "packet.h"

#include <string.h>

/* Byte-wise little-endian store: independent of host endianness. */
static void store_le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

void viture_ot_encode(const double ypr[3], uint32_t frame, uint8_t out[VITURE_OT_PACKET_SIZE])
{
    const double d[6] = {0.0, 0.0, 0.0, ypr[0], ypr[1], ypr[2]};
    for (int i = 0; i < 6; i++) {
        uint64_t bits;
        memcpy(&bits, &d[i], sizeof bits);
        store_le64(out + 8 * i, bits);
    }
    for (int i = 0; i < 4; i++)
        out[48 + i] = (uint8_t)(frame >> (8 * i));
}
