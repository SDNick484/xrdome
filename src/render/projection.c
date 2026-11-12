#include "render/projection.h"

#include <math.h>

#define XRD_PI 3.14159265358979323846

void xrd_fov_tan_half(float diag_deg, float aspect, float zoom, float *tan_h, float *tan_v)
{
    double half = (double)diag_deg * XRD_PI / 360.0;
    double t = tan(half);
    double norm = sqrt((double)aspect * aspect + 1.0);
    *tan_h = (float)(t * aspect / norm / zoom);
    *tan_v = (float)(t / norm / zoom);
}

bool xrd_dir_to_uv(const float d[3], float uv[2])
{
    double x = d[0], y = d[1], z = d[2];
    double len = sqrt(x * x + y * y + z * z);
    if (!isfinite(len) || len == 0.0)
        return false;
    if (z > 0.0)
        return false;
    x /= len;
    y /= len;
    z /= len;

    /* At the poles x and z are both ~0 and atan2 is arbitrary; pick forward. */
    double lon = (x == 0.0 && z == 0.0) ? 0.0 : atan2(x, -z);
    double sy = y < -1.0 ? -1.0 : (y > 1.0 ? 1.0 : y);
    double lat = asin(sy);

    double u = lon / XRD_PI + 0.5;
    double v = lat / XRD_PI + 0.5;
    uv[0] = (float)(u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u));
    uv[1] = (float)(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
    return true;
}

void xrd_eye_u_transform(xrd_layout layout, int eye, bool swap, float *scale, float *offset)
{
    if (layout == XRD_LAYOUT_MONO) {
        *scale = 1.0f;
        *offset = 0.0f;
        return;
    }
    int right_half = (eye != 0);
    if (layout == XRD_LAYOUT_SBS_RL)
        right_half = !right_half;
    if (swap)
        right_half = !right_half;
    *scale = 0.5f;
    *offset = right_half ? 0.5f : 0.0f;
}
