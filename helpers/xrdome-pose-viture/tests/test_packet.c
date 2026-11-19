#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "packet.h"
#include "pose/opentrack_packet.h"

/* The helper's encoder against xrdome's own parser. */

static void round_trip(void **state)
{
    (void)state;
    const double ypr[3] = {-157.25, 12.5, 179.875};
    uint8_t buf[VITURE_OT_PACKET_SIZE];
    viture_ot_encode(ypr, 0x01020304u, buf);
    assert_int_equal(VITURE_OT_PACKET_SIZE, XRD_OT_PACKET_SIZE);

    double out[6];
    uint32_t frame = 0;
    assert_int_equal(xrd_opentrack_parse(buf, sizeof buf, out, &frame), 0);
    assert_true(out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
    assert_true(out[3] == ypr[0] && out[4] == ypr[1] && out[5] == ypr[2]);
    assert_int_equal(frame, 0x01020304u);
}

static void layout_is_little_endian(void **state)
{
    (void)state;
    /* 1.0 is 0x3FF0000000000000: bytes 00 .. 00 F0 3F, low byte first. */
    const double ypr[3] = {1.0, 0.0, 0.0};
    uint8_t buf[VITURE_OT_PACKET_SIZE];
    viture_ot_encode(ypr, 5, buf);
    const uint8_t yaw[8] = {0, 0, 0, 0, 0, 0, 0xF0, 0x3F};
    assert_memory_equal(buf + 24, yaw, 8);
    const uint8_t frame[4] = {5, 0, 0, 0};
    assert_memory_equal(buf + 48, frame, 4);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(round_trip),
        cmocka_unit_test(layout_is_little_endian),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
