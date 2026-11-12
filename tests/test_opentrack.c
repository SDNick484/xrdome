#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <cmocka.h>

#include "pose/opentrack_packet.h"

/* Host-endianness-independent only on little-endian hosts, which is all the
 * tests target; the parser itself does not depend on host endianness. */
static void pack(uint8_t buf[XRD_OT_PACKET_SIZE], const double d[6], uint32_t frame)
{
    memcpy(buf, d, 48);
    memcpy(buf + 48, &frame, 4);
}

static void parses_fields(void **state)
{
    (void)state;
    const double in[6] = {1.5, -2.5, 3.5, 10.0, -5.0, 2.0};
    uint8_t buf[XRD_OT_PACKET_SIZE];
    pack(buf, in, 7);

    double out[6] = {0};
    uint32_t frame = 0;
    assert_int_equal(xrd_opentrack_parse(buf, sizeof buf, out, &frame), 0);
    for (int i = 0; i < 6; i++)
        assert_true(out[i] == in[i]); /* exact: bit-for-bit copy */
    assert_int_equal(frame, 7);
}

static void frame_may_be_null(void **state)
{
    (void)state;
    const double in[6] = {0, 0, 0, 1, 2, 3};
    uint8_t buf[XRD_OT_PACKET_SIZE];
    pack(buf, in, 1);
    double out[6];
    assert_int_equal(xrd_opentrack_parse(buf, sizeof buf, out, NULL), 0);
}

static void rejects_wrong_length(void **state)
{
    (void)state;
    const double in[6] = {0, 0, 0, 10, -5, 2};
    uint8_t buf[XRD_OT_PACKET_SIZE + 1] = {0};
    pack(buf, in, 7);
    double out[6];
    uint32_t frame;
    assert_int_equal(xrd_opentrack_parse(buf, 51, out, &frame), -1);
    assert_int_equal(xrd_opentrack_parse(buf, 53, out, &frame), -1);
    assert_int_equal(xrd_opentrack_parse(buf, 0, out, &frame), -1);
}

static void rejects_nan_yaw(void **state)
{
    (void)state;
    double in[6] = {0, 0, 0, NAN, 0, 0};
    uint8_t buf[XRD_OT_PACKET_SIZE];
    pack(buf, in, 1);
    double out[6];
    uint32_t frame;
    assert_int_equal(xrd_opentrack_parse(buf, sizeof buf, out, &frame), -1);
}

static void rejects_infinite_x(void **state)
{
    (void)state;
    double in[6] = {INFINITY, 0, 0, 0, 0, 0};
    uint8_t buf[XRD_OT_PACKET_SIZE];
    pack(buf, in, 1);
    double out[6];
    uint32_t frame;
    assert_int_equal(xrd_opentrack_parse(buf, sizeof buf, out, &frame), -1);
    in[0] = -INFINITY;
    pack(buf, in, 1);
    assert_int_equal(xrd_opentrack_parse(buf, sizeof buf, out, &frame), -1);
}

static void rejects_leave_outputs_untouched(void **state)
{
    (void)state;
    double in[6] = {0, 0, 0, NAN, 0, 0};
    uint8_t buf[XRD_OT_PACKET_SIZE];
    pack(buf, in, 99);
    double out[6] = {42, 42, 42, 42, 42, 42};
    uint32_t frame = 5;
    assert_int_equal(xrd_opentrack_parse(buf, sizeof buf, out, &frame), -1);
    assert_true(out[3] == 42.0);
    assert_int_equal(frame, 5);
}

static void parses_known_bytes(void **state)
{
    (void)state;
    /* 1.0 = 0x3FF0000000000000 little-endian; frame 0x01020304. */
    uint8_t buf[XRD_OT_PACKET_SIZE] = {0};
    buf[6] = 0xF0;
    buf[7] = 0x3F;
    buf[48] = 0x04;
    buf[49] = 0x03;
    buf[50] = 0x02;
    buf[51] = 0x01;
    double out[6];
    uint32_t frame;
    assert_int_equal(xrd_opentrack_parse(buf, sizeof buf, out, &frame), 0);
    assert_true(out[0] == 1.0);
    assert_true(out[1] == 0.0);
    assert_int_equal(frame, 0x01020304u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(parses_fields),
        cmocka_unit_test(frame_may_be_null),
        cmocka_unit_test(rejects_wrong_length),
        cmocka_unit_test(rejects_nan_yaw),
        cmocka_unit_test(rejects_infinite_x),
        cmocka_unit_test(rejects_leave_outputs_untouched),
        cmocka_unit_test(parses_known_bytes),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
