#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <cmocka.h>

#include "pose/viture_proto.h"

#define TOL 1e-5

/* An IMU frame as the glasses send it (FF FC, the command layout), with a
 * device timestamp and the given data. */
static void make_imu(uint8_t out[64], uint32_t ts, const uint8_t *data, uint16_t len)
{
    assert_int_equal(xrd_viture_cmd(0, data, len, out), 64);
    out[1] = 0xFC;
    out[6] = (uint8_t)(ts & 0xFF);
    out[7] = (uint8_t)((ts >> 8) & 0xFF);
    out[8] = (uint8_t)((ts >> 16) & 0xFF);
    out[9] = (uint8_t)(ts >> 24);
    uint16_t field = (uint16_t)(out[4] | (out[5] << 8));
    uint16_t crc = xrd_viture_crc(out + 4, (size_t)field + 2);
    out[2] = (uint8_t)(crc & 0xFF);
    out[3] = (uint8_t)(crc >> 8);
}

static void crc_ccitt_check(void **state)
{
    (void)state;
    /* The CRC-16/XMODEM check value (poly 0x1021, init 0). */
    assert_int_equal(xrd_viture_crc((const uint8_t *)"123456789", 9), 0x31C3);
    assert_int_equal(xrd_viture_crc(NULL, 0), 0);
}

static void cmd_imu_on_layout(void **state)
{
    (void)state;
    uint8_t out[64];
    memset(out, 0xAA, sizeof out);
    const uint8_t on = 1;
    assert_int_equal(xrd_viture_cmd(XRD_VITURE_CMD_IMU, &on, 1, out), 64);
    assert_int_equal(out[0], 0xFF);
    assert_int_equal(out[1], 0xFE);
    assert_int_equal(out[4], 13);
    assert_int_equal(out[5], 0);
    for (int i = 6; i < 0x0E; i++)
        assert_int_equal(out[i], 0);
    assert_int_equal(out[0x0E], 0x15);
    assert_int_equal(out[0x0F], 0x00);
    assert_int_equal(out[0x10], 0);
    assert_int_equal(out[0x11], 0);
    assert_int_equal(out[0x12], 0x01);
    for (int i = 0x13; i < 64; i++)
        assert_int_equal(out[i], 0);
    uint16_t crc = xrd_viture_crc(out + 4, 2 + 13);
    assert_int_equal(out[2], crc & 0xFF);
    assert_int_equal(out[3], crc >> 8);
}

static void cmd_too_long(void **state)
{
    (void)state;
    uint8_t data[47] = {0}, out[64];
    assert_int_equal(xrd_viture_cmd(1, data, 47, out), 0);
    assert_int_equal(xrd_viture_cmd(1, data, 46, out), 64);
    assert_int_equal(xrd_viture_cmd(1, NULL, 0, out), 64);
    assert_int_equal(out[4], 12);
}

static void parse_roundtrip(void **state)
{
    (void)state;
    uint8_t buf[64];
    const uint8_t on = 1;
    xrd_viture_cmd(XRD_VITURE_CMD_IMU, &on, 1, buf);
    xrd_viture_msg m;
    assert_true(xrd_viture_parse(buf, sizeof buf, &m));
    assert_int_equal(m.kind, 0xFE);
    assert_int_equal(m.cmd_id, 0x15);
    assert_int_equal(m.len, 1);
    assert_int_equal(m.data[0], 1);
    assert_ptr_equal(m.data, buf + 0x12);
    assert_int_equal(m.ts, 0);

    const uint8_t d[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    make_imu(buf, 0x12345678u, d, sizeof d);
    assert_true(xrd_viture_parse(buf, sizeof buf, &m));
    assert_int_equal(m.kind, 0xFC);
    assert_int_equal(m.ts, 0x12345678u);
    assert_int_equal(m.len, 12);
    assert_memory_equal(m.data, d, 12);

    /* A buffer exactly as long as the frame is enough. */
    assert_true(xrd_viture_parse(buf, 0x12 + 12, &m));
}

static void parse_bad_crc(void **state)
{
    (void)state;
    uint8_t buf[64];
    const uint8_t on = 1;
    xrd_viture_cmd(XRD_VITURE_CMD_IMU, &on, 1, buf);
    xrd_viture_msg m;
    buf[0x12] ^= 0x01; /* data */
    assert_false(xrd_viture_parse(buf, sizeof buf, &m));
    buf[0x12] ^= 0x01;
    buf[2] ^= 0x80; /* the CRC itself */
    assert_false(xrd_viture_parse(buf, sizeof buf, &m));
    buf[2] ^= 0x80;
    assert_true(xrd_viture_parse(buf, sizeof buf, &m));
    /* Bytes past the frame are not covered: padding may differ. */
    buf[40] = 0x5A;
    assert_true(xrd_viture_parse(buf, sizeof buf, &m));
}

static void parse_bad_header(void **state)
{
    (void)state;
    uint8_t buf[64];
    xrd_viture_cmd(1, NULL, 0, buf);
    xrd_viture_msg m;
    buf[0] = 0xFE;
    assert_false(xrd_viture_parse(buf, sizeof buf, &m));
    buf[0] = 0xFF;
    buf[1] = 0xFD;
    assert_false(xrd_viture_parse(buf, sizeof buf, &m));
    buf[1] = 0xFE;
    assert_true(xrd_viture_parse(buf, sizeof buf, &m));
}

static void parse_truncated(void **state)
{
    (void)state;
    uint8_t buf[64];
    const uint8_t d[12] = {0};
    make_imu(buf, 0, d, sizeof d);
    xrd_viture_msg m;
    assert_false(xrd_viture_parse(buf, 0, &m));
    assert_false(xrd_viture_parse(buf, 17, &m));
    assert_false(xrd_viture_parse(buf, 0x12 + 11, &m)); /* frame reaches past the buffer */
    assert_false(xrd_viture_parse(NULL, 64, &m));

    /* A length field below 12, with a CRC that matches it. */
    xrd_viture_cmd(1, NULL, 0, buf);
    buf[4] = 11;
    uint16_t crc = xrd_viture_crc(buf + 4, 11 + 2);
    buf[2] = (uint8_t)(crc & 0xFF);
    buf[3] = (uint8_t)(crc >> 8);
    assert_false(xrd_viture_parse(buf, sizeof buf, &m));

    /* A length field past the 64-byte report. */
    buf[4] = 59;
    assert_false(xrd_viture_parse(buf, sizeof buf, &m));
}

static void imu_big_endian(void **state)
{
    (void)state;
    const uint8_t d[12] = {0x3F, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
                           0xC0, 0x40, 0x00, 0x00};
    uint8_t buf[64];
    make_imu(buf, 7, d, sizeof d);
    xrd_viture_msg m;
    assert_true(xrd_viture_parse(buf, sizeof buf, &m));
    float r, p, y;
    assert_true(xrd_viture_imu_ypr(&m, &r, &p, &y));
    assert_float_equal(r, 1.0f, TOL);
    assert_float_equal(p, 2.0f, TOL);
    assert_float_equal(y, -3.0f, TOL);

    m.len = 11;
    assert_false(xrd_viture_imu_ypr(&m, &r, &p, &y));
}

static void rotate_fwd(xrd_quat q, float out[3])
{
    const float fwd[3] = {0, 0, -1};
    xrd_quat_rotate(q, fwd, out);
}

static void to_gl_level_forward(void **state)
{
    (void)state;
    xrd_quat q = xrd_viture_to_gl(0, 0, 0);
    assert_float_equal(q.w, 1.0f, 1e-6);
    assert_float_equal(q.x, 0.0f, 1e-6);
    assert_float_equal(q.y, 0.0f, 1e-6);
    assert_float_equal(q.z, 0.0f, 1e-6);
}

static void to_gl_yaw_left(void **state)
{
    (void)state;
    float v[3];
    rotate_fwd(xrd_viture_to_gl(0, 0, 30), v);
    assert_float_equal(v[0], -0.5f, 1e-5);       /* toward -x: looking left */
    assert_float_equal(v[1], 0.0f, 1e-5);
    assert_float_equal(v[2], -0.8660254f, 1e-5);
}

static void to_gl_pitch_nwu(void **state)
{
    (void)state;
    float v[3];
    rotate_fwd(xrd_viture_to_gl(0, 20, 0), v);
    const double r = 20.0 * 3.14159265358979323846 / 180.0;
    assert_float_equal(v[0], 0.0f, 1e-5);
    assert_float_equal(v[1], (float)-sin(r), 1e-5); /* toward -y: nose down */
    assert_float_equal(v[2], (float)-cos(r), 1e-5);
}

static void to_gl_roll_nwu(void **state)
{
    (void)state;
    /* Roll +90 about NWU forward: the head's up goes to the right (+x). */
    const float up[3] = {0, 1, 0};
    float v[3];
    xrd_quat_rotate(xrd_viture_to_gl(90, 0, 0), up, v);
    assert_float_equal(v[0], 1.0f, 1e-5);
    assert_float_equal(v[1], 0.0f, 1e-5);
    assert_float_equal(v[2], 0.0f, 1e-5);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(crc_ccitt_check),
        cmocka_unit_test(cmd_imu_on_layout),
        cmocka_unit_test(cmd_too_long),
        cmocka_unit_test(parse_roundtrip),
        cmocka_unit_test(parse_bad_crc),
        cmocka_unit_test(parse_bad_header),
        cmocka_unit_test(parse_truncated),
        cmocka_unit_test(imu_big_endian),
        cmocka_unit_test(to_gl_level_forward),
        cmocka_unit_test(to_gl_yaw_left),
        cmocka_unit_test(to_gl_pitch_nwu),
        cmocka_unit_test(to_gl_roll_nwu),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
