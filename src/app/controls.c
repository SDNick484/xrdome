#include "app/controls.h"

#include <SDL_keycode.h>
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
    xrd_controls_init_sharpen(c, 0.0f);
    xrd_controls_init_depth(c, XRD_DEPTH_STRENGTH_DEFAULT, XRD_DEPTH_PLANE_DEFAULT);
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
    if (c->proj != XRD_PROJ_FLAT || !c->screen_curved)
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

void xrd_controls_cycle_filter(xrd_controls *c)
{
    c->filter = (xrd_filter)((c->filter + 1) % XRD_FILTER_COUNT);
}

void xrd_controls_init_sharpen(xrd_controls *c, float strength)
{
    c->sharpen = strength > 0.0f ? fminf(strength, XRD_SHARPEN_MAX) : 0.0f;
}

void xrd_controls_step_sharpen(xrd_controls *c)
{
    /* The tolerance keeps 0.5 stepping to 0.75, not to 0.5 again. */
    float next = (floorf(c->sharpen / XRD_SHARPEN_STEP + 1e-3f) + 1.0f) * XRD_SHARPEN_STEP;
    c->sharpen = next > XRD_SHARPEN_MAX + 1e-3f ? 0.0f : fminf(next, XRD_SHARPEN_MAX);
}

void xrd_controls_init_depth(xrd_controls *c, float strength, float plane)
{
    c->depth_on = true;
    c->depth_strength = strength > 0.0f ? fminf(strength, XRD_DEPTH_STRENGTH_MAX) : 0.0f;
    c->depth_plane = plane > 0.0f ? fminf(plane, 1.0f) : 0.0f;
}

void xrd_controls_toggle_depth(xrd_controls *c)
{
    c->depth_on = !c->depth_on;
}

/* v moved by dir steps of step, onto a multiple of step, within lo .. hi. A
 * value between multiples goes to the next one in that direction. */
static float step_onto(float v, float step, int dir, float lo, float hi)
{
    float q = v / step;
    float n = dir > 0 ? floorf(q + 1e-3f) + 1.0f : ceilf(q - 1e-3f) - 1.0f;
    float out = n * step;
    /* n * step is not always the float the limit is: at or past a limit
     * within rounding, the limit itself, so a press there changes nothing. */
    if (out <= lo + step * 1e-3f)
        return lo;
    if (out >= hi - step * 1e-3f)
        return hi;
    return out;
}

void xrd_controls_depth_weaker(xrd_controls *c)
{
    c->depth_strength = step_onto(c->depth_strength, XRD_DEPTH_STRENGTH_STEP, -1, 0.0f,
                                  XRD_DEPTH_STRENGTH_MAX);
}

void xrd_controls_depth_stronger(xrd_controls *c)
{
    c->depth_strength = step_onto(c->depth_strength, XRD_DEPTH_STRENGTH_STEP, 1, 0.0f,
                                  XRD_DEPTH_STRENGTH_MAX);
}

void xrd_controls_plane_further(xrd_controls *c)
{
    c->depth_plane = step_onto(c->depth_plane, XRD_DEPTH_PLANE_STEP, -1, 0.0f, 1.0f);
}

void xrd_controls_plane_nearer(xrd_controls *c)
{
    c->depth_plane = step_onto(c->depth_plane, XRD_DEPTH_PLANE_STEP, 1, 0.0f, 1.0f);
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

const xrd_key_help xrd_key_list[] = {
    {"Space", "pause / resume"},
    {"Left / Right", "seek -10 s / +10 s"},
    {"Up / Down", "seek +60 s / -60 s"},
    {"PageDown / PageUp", "next / previous photo (a directory)"},
    {"R", "recenter, yaw only"},
    {"Shift+R", "recenter, full"},
    {"[ / ]", "zoom out / in"},
    {"F", "cycle the enlarging filter"},
    {"Shift+F", "sharpening: off, 0.25, 0.5, 0.75, 1"},
    {"S", "swap eyes"},
    {"M", "cycle layout (flat: 2D, half SBS, full SBS, half TB, full TB)"},
    {"P", "projection: flat / VR180"},
    {"Z", "zoom mode: screen / magnify (flat)"},
    {"L", "screen: fixed / head-locked (flat)"},
    {"B", "debanding on / off"},
    {"C", "screen: flat / curved (flat)"},
    {"D", "depth 3D on / off (a photo with depth)"},
    {"- / =", "depth strength down / up"},
    {", / .", "screen plane further / nearer (depth 3D)"},
    {"?", "this list"},
    {"I", "playback information"},
    {"Q", "quit"},
};
const size_t xrd_key_list_len = sizeof xrd_key_list / sizeof xrd_key_list[0];

bool xrd_key_repeats(int32_t sdl_keycode)
{
    switch (sdl_keycode) {
    case SDLK_LEFT:
    case SDLK_RIGHT:
    case SDLK_UP:
    case SDLK_DOWN:
    case SDLK_LEFTBRACKET:
    case SDLK_RIGHTBRACKET:
    case SDLK_MINUS:
    case SDLK_EQUALS:
    case SDLK_COMMA:
    case SDLK_PERIOD:
        return true;
    default:
        return false;
    }
}
