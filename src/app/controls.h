#ifndef XRD_APP_CONTROLS_H
#define XRD_APP_CONTROLS_H

#include <stdbool.h>

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
    bool screen_curved;         /* C: the flat screen is curved (screen zoom mode only) */
    float screen_curve;         /* the curved screen's radius, XRD_SCREEN_CURVE_MIN..MAX */
    bool layout_chosen;         /* M or S was pressed in this run */
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
 * screen is curved, flat and in screen zoom mode; otherwise 0. Magnify mode
 * keeps the zoom-1 screen and narrows the view, so it has no large screen
 * to bend: the screen stays flat there. */
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
void xrd_controls_toggle_filter(xrd_controls *c); /* bicubic <-> bilinear (F) */
/* Left-button drag by (dx, dy) window pixels, SDL axes (+y down): dragging
 * right looks right, dragging up looks up, 0.1 degrees per pixel. */
void xrd_controls_mouse_drag(xrd_controls *c, int dx, int dy);
/* Zeroes the mouse angles (R / Shift+R). */
void xrd_controls_recenter(xrd_controls *c);
/* xrd_quat_from_ypr_deg(mouse_yaw, mouse_pitch, 0); multiply onto the
 * tracker's view: view = tracker_view * mouse_quat. */
xrd_quat xrd_controls_mouse_quat(const xrd_controls *c);

#endif
