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

/* Mirrors vr180.frag: keep the two in step. */
bool xrd_dir_to_uv_fov(const float d[3], float hfov_deg, float vfov_deg, float uv[2])
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

    double u = lon / ((double)hfov_deg * XRD_PI / 180.0) + 0.5;
    double v = lat / ((double)vfov_deg * XRD_PI / 180.0) + 0.5;
    /* An axis covering 180 degrees never rejects (only clamps): every front
     * direction is inside it, and GPU atan/asin error must not blacken pixels. */
    const double eps = 1e-6;
    const bool rej_u = hfov_deg < XRD_FULL_FOV_DEG, rej_v = vfov_deg < XRD_FULL_FOV_DEG;
    if ((rej_u && (u < -eps || u > 1.0 + eps)) || (rej_v && (v < -eps || v > 1.0 + eps)))
        return false;
    uv[0] = (float)(u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u));
    uv[1] = (float)(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
    return true;
}

bool xrd_dir_to_uv(const float d[3], float uv[2])
{
    return xrd_dir_to_uv_fov(d, 180.f, 180.f, uv);
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
