#ifndef XRD_APP_CONTROLS_H
#define XRD_APP_CONTROLS_H

#include <stdbool.h>

#include "math/quat.h"
#include "render/projection.h"
#include "render/renderer_gl.h"

/* View state the user changes at runtime with keys and the mouse. */
typedef struct {
    float zoom;             /* 0.25 .. 4; larger is a narrower view */
    xrd_layout layout;
    bool swap_eyes;
    double mouse_yaw_deg;   /* mouse look (null pose backend); positive = left */
    double mouse_pitch_deg; /* positive = up, clamped to +-89 */
    float convergence_deg;  /* -5 .. 5; positive toes the eyes in */
    float convergence_start; /* what xrd_controls_convergence_reset returns to */
    xrd_filter filter;
} xrd_controls;

#define XRD_ZOOM_MIN 0.25f
#define XRD_ZOOM_MAX 4.0f
#define XRD_ZOOM_STEP 1.1f
#define XRD_MOUSE_DEG_PER_PX 0.1
#define XRD_MOUSE_PITCH_MAX 89.0
#define XRD_CONVERGENCE_MAX 5.0f /* and -5 */
#define XRD_CONVERGENCE_STEP 0.1f

/* Convergence 0 (also the reset value), filter bicubic. */
void xrd_controls_init(xrd_controls *c, float zoom, xrd_layout layout);
/* Sets the starting convergence (also the reset value) and filter. */
void xrd_controls_init_display(xrd_controls *c, float convergence_deg, xrd_filter filter);
void xrd_controls_zoom_in(xrd_controls *c);       /* zoom * 1.1, at most 4 */
void xrd_controls_zoom_out(xrd_controls *c);      /* zoom / 1.1, at least 0.25 */
void xrd_controls_cycle_layout(xrd_controls *c);  /* sbs-lr -> sbs-rl -> mono -> sbs-lr */
void xrd_controls_toggle_swap(xrd_controls *c);
/* Convergence +- 0.1 degrees, clamped to -5..5 (. and ,). */
void xrd_controls_convergence_inc(xrd_controls *c);
void xrd_controls_convergence_dec(xrd_controls *c);
/* Back to the starting convergence (/). */
void xrd_controls_convergence_reset(xrd_controls *c);
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
