#ifndef XRD_APP_CONTROLS_H
#define XRD_APP_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "math/quat.h"
#include "render/projection.h"
#include "render/renderer_gl.h"
#include "values.h"

/* View state the user changes at runtime with keys and the mouse. */
typedef struct {
    float zoom;             /* 0.25 .. 4; larger is a narrower view */
    xrd_layout layout;
    bool swap_eyes;
    double mouse_yaw_deg;   /* mouse look (null pose backend); positive = left */
    double mouse_pitch_deg; /* positive = up, clamped to +-89 */
    float convergence_deg;  /* -5 .. 5; positive toes the eyes in */
    xrd_filter filter;
    xrd_proj proj;
    xrd_packing packing;        /* AUTO, HALF or FULL */
    float zoom_other;           /* the zoom of the projection not in effect */
    xrd_zoom_mode zoom_mode;
    bool screen_locked;
    bool screen_curved;         /* C: the flat screen is curved (both zoom modes) */
    float screen_curve;         /* the curved screen's radius, XRD_SCREEN_CURVE_MIN..MAX */
    float sharpen;              /* sharpening strength, 0 (off) to XRD_SHARPEN_MAX */
    bool layout_chosen;         /* M or S was pressed in this run */
    bool depth_on;              /* D: depth 3D (on at start for photos, off for films) */
    float depth_strength;       /* 0 .. XRD_DEPTH_STRENGTH_MAX, picture widths per unit of depth */
    float depth_plane;          /* 0 .. 1: the depth that sits on the screen */
} xrd_controls;

#define XRD_ZOOM_STEP 1.1f
#define XRD_MOUSE_DEG_PER_PX 0.1
#define XRD_MOUSE_PITCH_MAX 89.0
#define XRD_SEEK_SMALL_S 10.0
#define XRD_SEEK_LARGE_S 60.0

/* Convergence 0, filter bicubic. */
void xrd_controls_init(xrd_controls *c, float zoom, xrd_layout layout);
/* Sets the convergence (from --convergence or a saved record) and filter. */
void xrd_controls_init_display(xrd_controls *c, float convergence_deg, xrd_filter filter);
/* Sets the projection state: packing, the zoom of the other projection, zoom
 * mode and screen lock. xrd_controls_init leaves hequirect, AUTO, zoom_other
 * 1, SCREEN, unlocked. */
void xrd_controls_init_format(xrd_controls *c, xrd_proj proj, xrd_packing packing,
                              float zoom_other, xrd_zoom_mode mode, bool locked);
void xrd_controls_toggle_proj(xrd_controls *c);      /* flips proj, exchanges zoom and zoom_other */
bool xrd_controls_toggle_zoom_mode(xrd_controls *c); /* false, unchanged, in hequirect */
bool xrd_controls_toggle_lock(xrd_controls *c);      /* false, unchanged, in hequirect */
/* From --screen-curve: 0 (off) leaves the screen flat and C curves it with
 * radius 1; a radius in range curves it with that radius. Anything else
 * counts as 0. xrd_controls_init sets 0. */
void xrd_controls_init_curve(xrd_controls *c, float curve);
bool xrd_controls_toggle_curve(xrd_controls *c);     /* C; false, unchanged, in hequirect */
/* The curve to draw with (xrd_draw_params.screen_curve): the radius while the
 * screen is curved and flat; otherwise 0. Both zoom modes curve: magnify mode
 * curves its zoom-1 screen with the same radius and narrows the view onto it,
 * so Z never changes the curve. */
float xrd_controls_screen_curve(const xrd_controls *c);
void xrd_controls_zoom_in(xrd_controls *c);       /* zoom * 1.1, at most 4 */
void xrd_controls_zoom_out(xrd_controls *c);      /* zoom / 1.1, at least 0.25 */
/* M. hequirect: sbs-lr, sbs-rl, tb-lr, mono, back to sbs-lr. flat: mono,
 * (sbs-lr, half), (sbs-lr, full), (tb-lr, half), (tb-lr, full), back to mono;
 * the current position comes from the layout family and the resolved packing
 * (AUTO is resolved with frame_aspect), so an -rl layout steps to the next
 * entry. Sets layout_chosen. */
void xrd_controls_cycle_layout(xrd_controls *c, double frame_aspect);
void xrd_controls_toggle_swap(xrd_controls *c);   /* sets layout_chosen */
/* F: the next filter in xrd_filter order, after the last the first. */
void xrd_controls_cycle_filter(xrd_controls *c);
/* From --sharpen; values outside 0..XRD_SHARPEN_MAX are clamped into it.
 * xrd_controls_init sets 0. */
void xrd_controls_init_sharpen(xrd_controls *c, float strength);
/* Shift+F: the next multiple of XRD_SHARPEN_STEP above the strength (0.25,
 * 0.5, 0.75, 1; from 0.3, 0.5), and from XRD_SHARPEN_MAX back to 0 (off). */
void xrd_controls_step_sharpen(xrd_controls *c);
/* Depth 3D: on, with this strength and plane, clamped into their ranges.
 * xrd_controls_init sets XRD_DEPTH_STRENGTH_DEFAULT and XRD_DEPTH_PLANE_DEFAULT. */
void xrd_controls_init_depth(xrd_controls *c, float strength, float plane);
void xrd_controls_toggle_depth(xrd_controls *c);  /* D */
/* - / =: the strength one XRD_DEPTH_STRENGTH_STEP down / up, onto a multiple
 * of the step, within 0 .. XRD_DEPTH_STRENGTH_MAX. */
void xrd_controls_depth_weaker(xrd_controls *c);
void xrd_controls_depth_stronger(xrd_controls *c);
/* , / .: the screen plane one XRD_DEPTH_PLANE_STEP further (the plane's depth
 * down, so the picture comes forward) / nearer, onto a multiple of the step,
 * within 0 .. 1. */
void xrd_controls_plane_further(xrd_controls *c);
void xrd_controls_plane_nearer(xrd_controls *c);

/* Left-button drag by (dx, dy) window pixels, SDL axes (+y down): dragging
 * right looks right, dragging up looks up, 0.1 degrees per pixel. */
void xrd_controls_mouse_drag(xrd_controls *c, int dx, int dy);
/* Zeroes the mouse angles (R / Shift+R). */
void xrd_controls_recenter(xrd_controls *c);
/* xrd_quat_from_ypr_deg(mouse_yaw, mouse_pitch, 0); multiply onto the
 * tracker's view: view = tracker_view * mouse_quat. */
xrd_quat xrd_controls_mouse_quat(const xrd_controls *c);

/* One line of the key list that ? prints: the key and what it does. */
typedef struct {
    const char *key;
    const char *action;
} xrd_key_help;

/* The key list, in the order of the Keys table in docs/usage.md
 * (test_controls checks the keys and the order against it). */
extern const xrd_key_help xrd_key_list[];
extern const size_t xrd_key_list_len;

/* True for the keys that act on key repeat (an SDL keycode): zoom, seek, and
 * the depth strength and plane.
 * Every other key acts once per press (docs/usage.md, Keys). */
bool xrd_key_repeats(int32_t sdl_keycode);

#endif
