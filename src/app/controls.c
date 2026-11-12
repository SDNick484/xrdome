#include "app/controls.h"

void xrd_controls_init(xrd_controls *c, float zoom, xrd_layout layout)
{
    c->zoom = zoom;
    c->layout = layout;
    c->swap_eyes = false;
    c->mouse_yaw_deg = 0.0;
    c->mouse_pitch_deg = 0.0;
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

void xrd_controls_cycle_layout(xrd_controls *c)
{
    switch (c->layout) {
    case XRD_LAYOUT_SBS_LR:
        c->layout = XRD_LAYOUT_SBS_RL;
        break;
    case XRD_LAYOUT_SBS_RL:
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
