#include "video/mpv_scale.h"

#include <math.h>

static bool pos_finite(double v)
{
    return isfinite(v) && v > 0.0;
}

bool xrd_mpv_scale_size(const xrd_view_geom *g, xrd_layout layout, int tex_w, int tex_h,
                        int out_eye_w, int out_eye_h, int max_tex, int *fbo_w, int *fbo_h)
{
    *fbo_w = tex_w;
    *fbo_h = tex_h;
    if (!g || tex_w <= 0 || tex_h <= 0 || out_eye_w <= 0 || out_eye_h <= 0)
        return false;
    if (!pos_finite(g->half_w) || !pos_finite(g->half_h) || !pos_finite(g->tan_h) ||
        !pos_finite(g->tan_v))
        return false;
    int nx = xrd_layout_is_sbs(layout) ? 2 : 1, ny = xrd_layout_is_tb(layout) ? 2 : 1;
    int ew = tex_w / nx, eh = tex_h / ny; /* one eye, as the eye pass reads it */
    if (ew <= 0 || eh <= 0)
        return false;
    /* Display pixels across the screen at the view centre: the view's
     * 2 tan spans out_eye pixels, the screen 2 half of the same tangent. */
    double dw = (double)g->half_w / g->tan_h * out_eye_w;
    double dh = (double)g->half_h / g->tan_v * out_eye_h;
    if (!pos_finite(dw) || !pos_finite(dh) || !(dw > ew && dh > eh))
        return false; /* shrunk or 1:1 on an axis: today's path */

    double k = 1.0; /* the cap, the same on both axes */
    double texels = nx * dw * ny * dh;
    if (texels > (double)XRD_MPV_SCALE_MAX_TEXELS)
        k = sqrt((double)XRD_MPV_SCALE_MAX_TEXELS / texels);
    if (max_tex > 0) {
        if (nx * dw * k > max_tex)
            k = max_tex / (nx * dw);
        if (ny * dh * k > max_tex)
            k = max_tex / (ny * dh);
    }
    /* Rounded to the nearest texel, or down when capped so the cap holds. */
    double w = k < 1.0 ? floor(dw * k) : floor(dw + 0.5);
    double h = k < 1.0 ? floor(dh * k) : floor(dh + 0.5);
    if (!(w > ew && h > eh))
        return false; /* the cap left nothing to enlarge */
    *fbo_w = nx * (int)w;
    *fbo_h = ny * (int)h;
    return true;
}

bool xrd_size_debounce_step(xrd_size_debounce *d, int w, int h, uint64_t now_ns,
                            uint64_t settle_ns)
{
    if (!d->started) {
        d->started = true;
        d->pending = false;
        bool changed = w != d->w || h != d->h;
        d->w = w;
        d->h = h;
        return changed;
    }
    if (w == d->w && h == d->h) {
        d->pending = false;
        return false;
    }
    if (!d->pending || w != d->want_w || h != d->want_h) {
        d->pending = true;
        d->want_w = w;
        d->want_h = h;
        d->since_ns = now_ns;
        return false;
    }
    if (now_ns - d->since_ns < settle_ns)
        return false;
    d->pending = false;
    d->w = w;
    d->h = h;
    return true;
}
