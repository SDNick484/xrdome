#ifndef XRD_PROJECTION_H
#define XRD_PROJECTION_H

#include <stdbool.h>

/* A content FOV at or above this counts as the full 180 degrees. */
#define XRD_FULL_FOV_DEG 179.999f

typedef enum { XRD_PROJ_HEQUIRECT, XRD_PROJ_FLAT } xrd_proj;
typedef enum { XRD_LAYOUT_SBS_LR, XRD_LAYOUT_SBS_RL, XRD_LAYOUT_MONO,
               XRD_LAYOUT_TB_LR, XRD_LAYOUT_TB_RL } xrd_layout;
typedef enum { XRD_PACKING_AUTO, XRD_PACKING_HALF, XRD_PACKING_FULL } xrd_packing;

bool xrd_layout_is_sbs(xrd_layout l);
bool xrd_layout_is_tb(xrd_layout l);

/* tan of half the horizontal and vertical FOV; zoom > 1 narrows the view.
 * diag_deg is the diagonal FOV. */
void xrd_fov_tan_half(float diag_deg, float aspect, float zoom, float *tan_h, float *tan_v);

/* Half-equirect lookup (GL axes, -z forward). d need not be normalised.
 * Returns false when the direction is behind the viewer (d[2] > 0) or is
 * zero-length / non-finite. On success uv is clamped to [0,1]. */
bool xrd_dir_to_uv(const float d[3], float uv[2]);

/* Like xrd_dir_to_uv for a video that covers hfov_deg x vfov_deg degrees,
 * centred straight ahead: u = lon / hfov + 0.5, v = lat / vfov + 0.5.
 * Returns false when behind the viewer, degenerate, or when u or v falls
 * outside [0,1] (a 1e-6 tolerance counts the exact edge as inside). An axis
 * whose FOV is 180 (>= XRD_FULL_FOV_DEG) is never rejected on, only clamped. On
 * success uv is clamped to [0,1]; for 180 x 180 it equals xrd_dir_to_uv.
 * hfov_deg and vfov_deg must be > 0. */
bool xrd_dir_to_uv_fov(const float d[3], float hfov_deg, float vfov_deg, float uv[2]);

typedef enum { XRD_ZOOM_SCREEN, XRD_ZOOM_MAGNIFY } xrd_zoom_mode;

/* HALF or FULL. mono gives FULL. AUTO: sbs is FULL when F >= 3.0, tb is FULL
 * when F < 1.5, otherwise HALF. */
xrd_packing xrd_packing_resolve(xrd_layout layout, xrd_packing packing, double frame_aspect);
/* A of spec 5.3 from a resolved packing. Returns 16.0/9.0 when frame_aspect
 * is not finite or not > 0. */
double xrd_picture_aspect(xrd_layout layout, xrd_packing resolved, double frame_aspect);

typedef struct {
    float tan_h, tan_v;    /* view half-FOV tangents for the eye pass */
    float half_w, half_h;  /* flat: screen half-extents on the plane z = -1; 0 for hequirect */
} xrd_view_geom;
/* hequirect: tan = display tan / zoom. flat SCREEN: tan = display tan,
 * S = zoom. flat MAGNIFY: tan = display tan / zoom, S = 1.
 * half_w = S * min(tan_h0, tan_v0 * A), half_h = half_w / A, with tan_h0 and
 * tan_v0 the display tangents at zoom 1.
 * A picture_aspect that is not finite or not > 0 counts as 16/9, and a zoom
 * that is not finite or not > 0 counts as 1, so out never holds NaN, an
 * infinity or a zero half-extent for a flat screen. */
void xrd_view_geometry(xrd_proj proj, xrd_zoom_mode mode, float fov_diag_deg, float eye_aspect,
                       float zoom, double picture_aspect, xrd_view_geom *out);

/* Mirrors the shader's flat branch. False when d[2] >= 0, d is degenerate, or
 * the hit is outside the screen (1e-6 tolerance counts the edge as inside).
 * Also false when half_w or half_h is not finite and > 0. On success uv is
 * clamped to [0,1]; v = 1 is the top edge of the screen. */
bool xrd_flat_dir_to_uv(const float d[3], float half_w, float half_h, float uv[2]);

/* Replaces xrd_eye_u_transform: tex = uv * scale + offset per axis.
 * tb-lr: the left eye is the top half (offset[1] = 0.5, t = 1 is the top).
 * eye is 0 for the left viewport, 1 for the right; swap exchanges the halves.
 * mono (and any value outside the enum) gives scale 1, offset 0 on both axes. */
void xrd_eye_uv_transform(xrd_layout layout, int eye, bool swap,
                          float scale[2], float offset[2]);

/* Source pixels per display pixel at the centre of the view, the larger of
 * the two axes; > 1 means the picture is being shrunk. eye_src_w/h is one
 * eye's sub-image in texels. content_*fov_deg are used for hequirect only.
 * Returns 0 when an input makes the ratio meaningless (a size or extent that
 * is not > 0, or not finite). */
float xrd_minify_ratio(xrd_proj proj, const xrd_view_geom *g, int eye_src_w, int eye_src_h,
                       int out_eye_w, int out_eye_h,
                       float content_hfov_deg, float content_vfov_deg);

#endif
