#ifndef XRD_VALUES_H
#define XRD_VALUES_H

/* Setting ranges and value parsers shared by the command line / config.ini
 * (config.c) and the per-file records (store/filestate.c). The parsers format
 * no messages; callers word their own errors. */

#include <stdbool.h>

#include "render/projection.h"

#define XRD_ZOOM_MIN 0.25f
#define XRD_ZOOM_MAX 4.0f
#define XRD_CONTENT_FOV_MIN 10.0f
#define XRD_CONTENT_FOV_MAX 180.0f
#define XRD_CONVERGENCE_MAX 5.0f /* and -5 */

typedef enum {
    XRD_VALUE_OK,
    XRD_VALUE_SYNTAX, /* not a number / not HxV / unknown name */
    XRD_VALUE_RANGE,  /* well formed but outside the allowed range */
} xrd_value_status;

/* Finite decimal float that consumes all of s. Leaves *out untouched on false. */
bool xrd_value_float(const char *s, float *out);

/* xrd_value_float plus lo <= v <= hi. *out is set only on OK. */
xrd_value_status xrd_value_ranged(const char *s, float lo, float hi, float *out);

/* "HxV" in degrees, each XRD_CONTENT_FOV_MIN..MAX. *h and *v are set only on OK. */
xrd_value_status xrd_value_content_fov(const char *s, float *h, float *v);

/* "sbs-lr", "sbs-rl", "tb-lr", "tb-rl" or "mono". */
bool xrd_value_layout(const char *s, xrd_layout *out);
const char *xrd_value_layout_name(xrd_layout layout); /* NULL if out of enum range */

/* "hequirect" or "flat". "auto" is handled by config.c, not here. */
bool xrd_value_proj(const char *s, xrd_proj *out);
const char *xrd_value_proj_name(xrd_proj p);           /* NULL if out of enum range */

/* "half" or "full"; the name of AUTO is "auto". */
bool xrd_value_packing(const char *s, xrd_packing *out);
const char *xrd_value_packing_name(xrd_packing p);     /* NULL if out of enum range */

/* "screen" or "magnify". */
bool xrd_value_zoom_mode(const char *s, xrd_zoom_mode *out);
const char *xrd_value_zoom_mode_name(xrd_zoom_mode m); /* NULL if out of enum range */

/* "yes" or "no". */
bool xrd_value_yesno(const char *s, bool *out);

#endif
