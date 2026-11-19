#include "convert.h"

#include <math.h>

#define R2D (180.0 / 3.14159265358979323846)

int viture_quat_to_ypr(const float q[4], double ypr[3])
{
    double w = q[0], x = q[1], y = q[2], z = q[3];
    if (!isfinite(w) || !isfinite(x) || !isfinite(y) || !isfinite(z))
        return -1;
    double n = sqrt(w * w + x * x + y * y + z * z);
    if (!(n > 1e-12))
        return -1;
    w /= n;
    x /= n;
    y /= n;
    z /= n;

    /* The rotation matrix of q = Rz(yaw) Ry(pitch) Rx(roll) has
     * m20 = -sin(pitch), m10 = cos(pitch) sin(yaw), m00 = cos(pitch) cos(yaw),
     * m21 = cos(pitch) sin(roll), m22 = cos(pitch) cos(roll). */
    double sinp = 2.0 * (w * y - z * x);
    if (sinp > 1.0)
        sinp = 1.0;
    else if (sinp < -1.0)
        sinp = -1.0;
    double yaw = atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
    double roll = atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
    ypr[0] = yaw * R2D;
    ypr[1] = asin(sinp) * R2D;
    ypr[2] = roll * R2D;
    return 0;
}

int viture_pose_to_ypr(const float data[7], double ypr[3])
{
    return viture_quat_to_ypr(data + 3, ypr);
}
