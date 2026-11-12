#include "math/quat.h"

#include <math.h>

xrd_quat xrd_quat_mul(xrd_quat a, xrd_quat b)
{
    xrd_quat r = {
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
    return r;
}

xrd_quat xrd_quat_conj(xrd_quat q)
{
    xrd_quat r = {q.w, -q.x, -q.y, -q.z};
    return r;
}

xrd_quat xrd_quat_normalize(xrd_quat q)
{
    double n2 = (double)q.w * q.w + (double)q.x * q.x +
                (double)q.y * q.y + (double)q.z * q.z;
    if (!isfinite(n2) || n2 <= 0.0)
        return XRD_QUAT_IDENTITY;
    double inv = 1.0 / sqrt(n2);
    xrd_quat r = {(float)(q.w * inv), (float)(q.x * inv),
                  (float)(q.y * inv), (float)(q.z * inv)};
    return r;
}

static xrd_quat axis_angle(double ax, double ay, double az, double rad)
{
    double s = sin(rad * 0.5);
    xrd_quat r = {(float)cos(rad * 0.5), (float)(ax * s),
                  (float)(ay * s), (float)(az * s)};
    return r;
}

xrd_quat xrd_quat_from_ypr_deg(double yaw, double pitch, double roll)
{
    const double d2r = 3.14159265358979323846 / 180.0;
    xrd_quat qy = axis_angle(0, 1, 0, yaw * d2r);
    xrd_quat qx = axis_angle(1, 0, 0, pitch * d2r);
    xrd_quat qz = axis_angle(0, 0, 1, roll * d2r);
    return xrd_quat_normalize(xrd_quat_mul(xrd_quat_mul(qy, qx), qz));
}

xrd_quat xrd_quat_yaw_only(xrd_quat q)
{
    const float fwd[3] = {0.f, 0.f, -1.f};
    float f[3];
    xrd_quat_rotate(q, fwd, f);
    /* Looking straight up/down: heading is undefined. */
    if (hypotf(f[0], f[2]) < 1e-6f)
        return XRD_QUAT_IDENTITY;
    double yaw = atan2(-(double)f[0], -(double)f[2]);
    return axis_angle(0, 1, 0, yaw);
}

void xrd_quat_rotate(xrd_quat q, const float v[3], float out[3])
{
    /* v' = v + 2w(u x v) + 2 u x (u x v), u = (x, y, z) */
    float ux = q.x, uy = q.y, uz = q.z;
    float cx = uy * v[2] - uz * v[1];
    float cy = uz * v[0] - ux * v[2];
    float cz = ux * v[1] - uy * v[0];
    float dx = uy * cz - uz * cy;
    float dy = uz * cx - ux * cz;
    float dz = ux * cy - uy * cx;
    out[0] = v[0] + 2.f * (q.w * cx + dx);
    out[1] = v[1] + 2.f * (q.w * cy + dy);
    out[2] = v[2] + 2.f * (q.w * cz + dz);
}

void xrd_quat_to_mat3(xrd_quat q, float m[9])
{
    float w = q.w, x = q.x, y = q.y, z = q.z;
    /* column 0 */
    m[0] = 1.f - 2.f * (y * y + z * z);
    m[1] = 2.f * (x * y + w * z);
    m[2] = 2.f * (x * z - w * y);
    /* column 1 */
    m[3] = 2.f * (x * y - w * z);
    m[4] = 1.f - 2.f * (x * x + z * z);
    m[5] = 2.f * (y * z + w * x);
    /* column 2 */
    m[6] = 2.f * (x * z + w * y);
    m[7] = 2.f * (y * z - w * x);
    m[8] = 1.f - 2.f * (x * x + y * y);
}
