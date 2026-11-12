#ifndef XRD_PROJECTION_H
#define XRD_PROJECTION_H

#include <stdbool.h>

typedef enum { XRD_LAYOUT_SBS_LR, XRD_LAYOUT_SBS_RL, XRD_LAYOUT_MONO } xrd_layout;

/* tan of half the horizontal and vertical FOV; zoom > 1 narrows the view.
 * diag_deg is the diagonal FOV. */
void xrd_fov_tan_half(float diag_deg, float aspect, float zoom, float *tan_h, float *tan_v);

/* Half-equirect lookup (GL axes, -z forward). d need not be normalised.
 * Returns false when the direction is behind the viewer (d[2] > 0) or is
 * zero-length / non-finite. On success uv is clamped to [0,1]. */
bool xrd_dir_to_uv(const float d[3], float uv[2]);

/* texture-space transform for one eye: u_tex = u * scale + offset */
void xrd_eye_u_transform(xrd_layout layout, int eye /* 0 left, 1 right */, bool swap,
                         float *scale, float *offset);

#endif
