#include "app/controls.h"

#include <math.h>

void xrd_controls_init(xrd_controls *c, float zoom, xrd_layout layout)
{
    c->zoom = zoom;
    c->layout = layout;
    c->swap_eyes = false;
    c->mouse_yaw_deg = 0.0;
    c->mouse_pitch_deg = 0.0;
    xrd_controls_init_display(c, 0.0f, XRD_FILTER_BICUBIC);
    xrd_controls_init_format(c, XRD_PROJ_HEQUIRECT, XRD_PACKING_AUTO, 1.0f, XRD_ZOOM_SCREEN, false);
    xrd_controls_init_curve(c, 0.0f);
    c->layout_chosen = false;
}

void xrd_controls_init_format(xrd_controls *c, xrd_proj proj, xrd_packing packing,
                              float zoom_other, xrd_zoom_mode mode, bool locked)
{
    c->proj = proj;
    c->packing = packing;
    c->zoom_other = zoom_other;
    c->zoom_mode = mode;
    c->screen_locked = locked;
}

void xrd_controls_toggle_proj(xrd_controls *c)
{
    float z = c->zoom;
    c->zoom = c->zoom_other;
    c->zoom_other = z;
    c->proj = c->proj == XRD_PROJ_FLAT ? XRD_PROJ_HEQUIRECT : XRD_PROJ_FLAT;
}

bool xrd_controls_toggle_zoom_mode(xrd_controls *c)
{
    if (c->proj != XRD_PROJ_FLAT)
        return false;
    c->zoom_mode = c->zoom_mode == XRD_ZOOM_SCREEN ? XRD_ZOOM_MAGNIFY : XRD_ZOOM_SCREEN;
    return true;
}

bool xrd_controls_toggle_lock(xrd_controls *c)
{
    if (c->proj != XRD_PROJ_FLAT)
        return false;
    c->screen_locked = !c->screen_locked;
    return true;
}

void xrd_controls_init_curve(xrd_controls *c, float curve)
{
    bool ok = curve >= XRD_SCREEN_CURVE_MIN && curve <= XRD_SCREEN_CURVE_MAX;
    c->screen_curved = ok;
    c->screen_curve = ok ? curve : XRD_SCREEN_CURVE_MIN;
}

bool xrd_controls_toggle_curve(xrd_controls *c)
{
    if (c->proj != XRD_PROJ_FLAT)
        return false;
    c->screen_curved = !c->screen_curved;
    return true;
}

float xrd_controls_screen_curve(const xrd_controls *c)
{
    if (c->proj != XRD_PROJ_FLAT || c->zoom_mode != XRD_ZOOM_SCREEN || !c->screen_curved)
        return 0.0f;
    return c->screen_curve;
}

void xrd_controls_init_display(xrd_controls *c, float convergence_deg, xrd_filter filter)
{
    c->convergence_deg = convergence_deg;
    c->filter = filter;
}

void xrd_controls_zoom_in(xrd_controls *c)
{
    c->zoom *= XRD_ZOOM_STEP;
    if (c->zoom > XRD_ZOOM_MAX)
        c->zoom = XRD_ZOOM_MAX;
}

void xrd_controls_zoom_out(xrd_controls *c)
{
    c->zoom /= XRD_ZOOM_STEP;
    if (c->zoom < XRD_ZOOM_MIN)
        c->zoom = XRD_ZOOM_MIN;
}

/* The flat cycle: (layout, packing) pairs; mono has no packing. */
static const struct { xrd_layout layout; xrd_packing packing; } flat_cycle[] = {
    { XRD_LAYOUT_MONO, XRD_PACKING_AUTO },
    { XRD_LAYOUT_SBS_LR, XRD_PACKING_HALF },
    { XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL },
    { XRD_LAYOUT_TB_LR, XRD_PACKING_HALF },
    { XRD_LAYOUT_TB_LR, XRD_PACKING_FULL },
};
#define N_FLAT_CYCLE (sizeof flat_cycle / sizeof flat_cycle[0])

void xrd_controls_cycle_layout(xrd_controls *c, double frame_aspect)
{
    c->layout_chosen = true;
    if (c->proj == XRD_PROJ_FLAT) {
        xrd_packing now = xrd_packing_resolve(c->layout, c->packing, frame_aspect);
        size_t at = 0; /* an unknown position steps to the first entry after mono */
        for (size_t i = 0; i < N_FLAT_CYCLE; i++) {
            bool same_family = (xrd_layout_is_sbs(c->layout) && xrd_layout_is_sbs(flat_cycle[i].layout))
                            || (xrd_layout_is_tb(c->layout) && xrd_layout_is_tb(flat_cycle[i].layout))
                            || (!xrd_layout_is_sbs(c->layout) && !xrd_layout_is_tb(c->layout) &&
                                flat_cycle[i].layout == XRD_LAYOUT_MONO);
            if (same_family && (flat_cycle[i].layout == XRD_LAYOUT_MONO || flat_cycle[i].packing == now)) {
                at = i;
                break;
            }
        }
        at = (at + 1) % N_FLAT_CYCLE;
        c->layout = flat_cycle[at].layout;
        c->packing = flat_cycle[at].packing;
        return;
    }
    switch (c->layout) {
    case XRD_LAYOUT_SBS_LR:
        c->layout = XRD_LAYOUT_SBS_RL;
        break;
    case XRD_LAYOUT_SBS_RL:
        c->layout = XRD_LAYOUT_TB_LR;
        break;
    case XRD_LAYOUT_TB_LR:
    case XRD_LAYOUT_TB_RL:
        c->layout = XRD_LAYOUT_MONO;
        break;
    case XRD_LAYOUT_MONO:
    default:
        c->layout = XRD_LAYOUT_SBS_LR;
        break;
    }
}

void xrd_controls_toggle_swap(xrd_controls *c)
{
    c->swap_eyes = !c->swap_eyes;
    c->layout_chosen = true;
}

void xrd_controls_toggle_filter(xrd_controls *c)
{
    c->filter = c->filter == XRD_FILTER_BICUBIC ? XRD_FILTER_BILINEAR : XRD_FILTER_BICUBIC;
}

void xrd_controls_mouse_drag(xrd_controls *c, int dx, int dy)
{
    /* Positive yaw turns left, so dragging right (dx > 0) lowers it; SDL's
     * y grows downward, so dragging up (dy < 0) raises the pitch. */
    c->mouse_yaw_deg -= dx * XRD_MOUSE_DEG_PER_PX;
    c->mouse_pitch_deg -= dy * XRD_MOUSE_DEG_PER_PX;
    if (c->mouse_pitch_deg > XRD_MOUSE_PITCH_MAX)
        c->mouse_pitch_deg = XRD_MOUSE_PITCH_MAX;
    if (c->mouse_pitch_deg < -XRD_MOUSE_PITCH_MAX)
        c->mouse_pitch_deg = -XRD_MOUSE_PITCH_MAX;
}

void xrd_controls_recenter(xrd_controls *c)
{
    c->mouse_yaw_deg = 0.0;
    c->mouse_pitch_deg = 0.0;
}

xrd_quat xrd_controls_mouse_quat(const xrd_controls *c)
{
    return xrd_quat_from_ypr_deg(c->mouse_yaw_deg, c->mouse_pitch_deg, 0.0);
}
