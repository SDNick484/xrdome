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

bool xrd_layout_is_sbs(xrd_layout l)
{
    return l == XRD_LAYOUT_SBS_LR || l == XRD_LAYOUT_SBS_RL;
}

bool xrd_layout_is_tb(xrd_layout l)
{
    return l == XRD_LAYOUT_TB_LR || l == XRD_LAYOUT_TB_RL;
}

void xrd_eye_uv_transform(xrd_layout layout, int eye, bool swap, float scale[2], float offset[2])
{
    scale[0] = scale[1] = 1.0f;
    offset[0] = offset[1] = 0.0f;
    bool sbs = xrd_layout_is_sbs(layout), tb = xrd_layout_is_tb(layout);
    if (!sbs && !tb)
        return; /* mono, or a value outside the enum */
    /* second: this eye reads the second half of the split axis (the right
     * half for sbs, the bottom half for tb: t = 1 is the image top). */
    bool second = (eye != 0);
    if (layout == XRD_LAYOUT_SBS_RL || layout == XRD_LAYOUT_TB_RL)
        second = !second;
    if (swap)
        second = !second;
    if (sbs) {
        scale[0] = 0.5f;
        offset[0] = second ? 0.5f : 0.0f;
    } else {
        scale[1] = 0.5f;
        offset[1] = second ? 0.0f : 0.5f;
    }
}

xrd_packing xrd_packing_resolve(xrd_layout layout, xrd_packing packing, double frame_aspect)
{
    bool sbs = xrd_layout_is_sbs(layout), tb = xrd_layout_is_tb(layout);
    if (!sbs && !tb)
        return XRD_PACKING_FULL;
    if (packing == XRD_PACKING_HALF || packing == XRD_PACKING_FULL)
        return packing;
    /* AUTO (or junk). A NaN frame aspect fails both tests and gives HALF. */
    if (sbs)
        return frame_aspect >= 3.0 ? XRD_PACKING_FULL : XRD_PACKING_HALF;
    return frame_aspect < 1.5 ? XRD_PACKING_FULL : XRD_PACKING_HALF;
}

#define XRD_ASPECT_FALLBACK (16.0 / 9.0)

static bool usable_aspect(double a)
{
    return isfinite(a) && a > 0.0;
}

double xrd_picture_aspect(xrd_layout layout, xrd_packing resolved, double frame_aspect)
{
    if (!usable_aspect(frame_aspect))
        return XRD_ASPECT_FALLBACK;
    resolved = xrd_packing_resolve(layout, resolved, frame_aspect);
    double a = frame_aspect;
    if (resolved == XRD_PACKING_FULL) {
        if (xrd_layout_is_sbs(layout))
            a = frame_aspect / 2.0;
        else if (xrd_layout_is_tb(layout))
            a = frame_aspect * 2.0;
    }
    /* a huge or denormal F can overflow or underflow on the way */
    return usable_aspect(a) ? a : XRD_ASPECT_FALLBACK;
}

void xrd_view_geometry(xrd_proj proj, xrd_zoom_mode mode, float fov_diag_deg, float eye_aspect,
                       float zoom, double picture_aspect, xrd_view_geom *out)
{
    if (!(isfinite(zoom) && zoom > 0.f))
        zoom = 1.f;
    if (proj != XRD_PROJ_FLAT) {
        xrd_fov_tan_half(fov_diag_deg, eye_aspect, zoom, &out->tan_h, &out->tan_v);
        out->half_w = out->half_h = 0.f;
        return;
    }
    float th0, tv0;
    xrd_fov_tan_half(fov_diag_deg, eye_aspect, 1.f, &th0, &tv0);
    double s;
    if (mode == XRD_ZOOM_MAGNIFY) {
        xrd_fov_tan_half(fov_diag_deg, eye_aspect, zoom, &out->tan_h, &out->tan_v);
        s = 1.0;
    } else {
        out->tan_h = th0;
        out->tan_v = tv0;
        s = zoom;
    }
    double a = usable_aspect(picture_aspect) ? picture_aspect : XRD_ASPECT_FALLBACK;
    double fit = (double)tv0 * a;
    double hw = s * ((double)th0 < fit ? (double)th0 : fit);
    out->half_w = (float)hw;
    out->half_h = (float)(hw / a);
}

/* Mirrors the flat branch of vr180.frag: keep the two in step. */
bool xrd_flat_dir_to_uv(const float d[3], float half_w, float half_h, float uv[2])
{
    if (!(isfinite(half_w) && half_w > 0.f && isfinite(half_h) && half_h > 0.f))
        return false;
    double x = d[0], y = d[1], z = d[2];
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
        return false;
    if (!(z < 0.0)) /* behind, beside, or the zero vector */
        return false;
    double u = x / -z / (2.0 * half_w) + 0.5;
    double v = y / -z / (2.0 * half_h) + 0.5;
    const double eps = 1e-6;
    if (!isfinite(u) || !isfinite(v) || u < -eps || u > 1.0 + eps || v < -eps || v > 1.0 + eps)
        return false;
    uv[0] = (float)(u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u));
    uv[1] = (float)(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
    return true;
}

static bool curve_in_range(float curve)
{
    return curve >= XRD_SCREEN_CURVE_MIN && curve <= XRD_SCREEN_CURVE_MAX;
}

/* Mirrors the flat branch of vr180.frag with u_flat_curve != 0: keep in step. */
bool xrd_flat_curve_dir_to_uv(const float d[3], float half_w, float half_h, float curve,
                              float uv[2])
{
    if (curve == 0.f)
        return xrd_flat_dir_to_uv(d, half_w, half_h, uv);
    if (!curve_in_range(curve))
        return false;
    if (!(isfinite(half_w) && half_w > 0.f && isfinite(half_h) && half_h > 0.f))
        return false;
    double x = d[0], y = d[1], z = d[2];
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
        return false;
    if (!(z < 0.0)) /* behind, beside, or the zero vector */
        return false;
    /* In the horizontal plane: the unit direction (hx, hz), the axis at
     * (0, a), and t the distance to the cylinder, the positive root of
     * t^2 - 2 a hz t + a^2 - R^2 = 0. The product of the roots is
     * -(R^2 - a^2) = -(2R - 1), so t = (2R - 1) / (s - a hz) with s the
     * discriminant's root: no cancellation, as a >= 0 and hz < 0. */
    double hl = sqrt(x * x + z * z);
    double hx = x / hl, hz = z / hl;
    double r = curve, a = r - 1.0, k = 2.0 * r - 1.0;
    double t = k / (sqrt(hz * hz * a * a + k) - hz * a);
    double phi = atan2(t * hx, a - t * hz);
    double u = r * phi / (2.0 * half_w) + 0.5;
    double v = y * t / hl / (2.0 * half_h) + 0.5;
    const double eps = 1e-6;
    if (!isfinite(u) || !isfinite(v) || u < -eps || u > 1.0 + eps || v < -eps || v > 1.0 + eps)
        return false;
    uv[0] = (float)(u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u));
    uv[1] = (float)(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
    return true;
}

double xrd_flat_half_angle(float half_w, float curve)
{
    if (!(isfinite(half_w) && half_w > 0.f))
        return 0.0;
    if (curve == 0.f)
        return atan((double)half_w);
    if (!curve_in_range(curve))
        return 0.0;
    /* The arc's end, seen from the eye, the axis curve - 1 behind it. */
    double r = curve, phi = (double)half_w / r;
    double ex = r * sin(phi), ez = (r - 1.0) - r * cos(phi);
    if (phi >= XRD_PI / 2.0 || !(ez < 0.0)) /* the end is beside or behind the viewer */
        return XRD_PI / 2.0;
    return atan2(ex, -ez);
}

static bool pos_finite(double v)
{
    return isfinite(v) && v > 0.0;
}

float xrd_minify_ratio(xrd_proj proj, const xrd_view_geom *g, int eye_src_w, int eye_src_h,
                       int out_eye_w, int out_eye_h,
                       float content_hfov_deg, float content_vfov_deg)
{
    if (!g || eye_src_w <= 0 || eye_src_h <= 0 || out_eye_w <= 0 || out_eye_h <= 0)
        return 0.f;
    if (!pos_finite(g->tan_h) || !pos_finite(g->tan_v))
        return 0.f;
    /* At the view centre one display pixel spans 2 tan / out in tangent space,
     * which is also its angle in radians there. */
    double dt_h = 2.0 * g->tan_h / out_eye_w, dt_v = 2.0 * g->tan_v / out_eye_h;
    double rh, rv;
    if (proj == XRD_PROJ_FLAT) {
        /* the screen plane at z = -1: 2 half_w of tangent is eye_src_w texels */
        if (!pos_finite(g->half_w) || !pos_finite(g->half_h))
            return 0.f;
        rh = dt_h * eye_src_w / (2.0 * g->half_w);
        rv = dt_v * eye_src_h / (2.0 * g->half_h);
    } else {
        if (!pos_finite(content_hfov_deg) || !pos_finite(content_vfov_deg))
            return 0.f;
        rh = dt_h * eye_src_w / (content_hfov_deg * XRD_PI / 180.0);
        rv = dt_v * eye_src_h / (content_vfov_deg * XRD_PI / 180.0);
    }
    double r = rh > rv ? rh : rv;
    return pos_finite(r) ? (float)r : 0.f;
}
