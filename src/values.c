#ifndef _GNU_SOURCE /* sdl2-compat's pkg-config cflags already define it */
#define _GNU_SOURCE /* strtof */
#endif

#include "values.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool xrd_value_float(const char *s, float *out)
{
    char *end;
    errno = 0;
    float v = strtof(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE || !isfinite(v))
        return false;
    *out = v;
    return true;
}

xrd_value_status xrd_value_ranged(const char *s, float lo, float hi, float *out)
{
    float v;
    if (!xrd_value_float(s, &v))
        return XRD_VALUE_SYNTAX;
    if (v < lo || v > hi)
        return XRD_VALUE_RANGE;
    *out = v;
    return XRD_VALUE_OK;
}

xrd_value_status xrd_value_content_fov(const char *s, float *h, float *v)
{
    const char *x = strchr(s, 'x');
    char hs[32];
    float hv, vv;
    if (!x || x <= s || (size_t)(x - s) >= sizeof hs)
        return XRD_VALUE_SYNTAX;
    memcpy(hs, s, (size_t)(x - s));
    hs[x - s] = '\0';
    /* strtof would also take leading blanks, signs and hex floats. */
    if (!(isdigit((unsigned char)hs[0]) || hs[0] == '.') ||
        !(isdigit((unsigned char)x[1]) || x[1] == '.') ||
        !xrd_value_float(hs, &hv) || !xrd_value_float(x + 1, &vv))
        return XRD_VALUE_SYNTAX;
    if (hv < XRD_CONTENT_FOV_MIN || hv > XRD_CONTENT_FOV_MAX || vv < XRD_CONTENT_FOV_MIN ||
        vv > XRD_CONTENT_FOV_MAX)
        return XRD_VALUE_RANGE;
    *h = hv;
    *v = vv;
    return XRD_VALUE_OK;
}

static const char *const layout_names[] = {
    [XRD_LAYOUT_SBS_LR] = "sbs-lr",
    [XRD_LAYOUT_SBS_RL] = "sbs-rl",
    [XRD_LAYOUT_MONO] = "mono",
    [XRD_LAYOUT_TB_LR] = "tb-lr",
    [XRD_LAYOUT_TB_RL] = "tb-rl",
};

bool xrd_value_layout(const char *s, xrd_layout *out)
{
    for (size_t i = 0; i < sizeof layout_names / sizeof layout_names[0]; i++)
        if (!strcmp(s, layout_names[i])) {
            *out = (xrd_layout)i;
            return true;
        }
    return false;
}

const char *xrd_value_layout_name(xrd_layout layout)
{
    if ((int)layout < 0 || (size_t)layout >= sizeof layout_names / sizeof layout_names[0])
        return NULL;
    return layout_names[layout];
}

static const char *const proj_names[] = {
    [XRD_PROJ_HEQUIRECT] = "hequirect",
    [XRD_PROJ_FLAT] = "flat",
};

bool xrd_value_proj(const char *s, xrd_proj *out)
{
    for (size_t i = 0; i < sizeof proj_names / sizeof proj_names[0]; i++)
        if (!strcmp(s, proj_names[i])) {
            *out = (xrd_proj)i;
            return true;
        }
    return false;
}

const char *xrd_value_proj_name(xrd_proj p)
{
    if ((int)p < 0 || (size_t)p >= sizeof proj_names / sizeof proj_names[0])
        return NULL;
    return proj_names[p];
}

static const char *const packing_names[] = {
    [XRD_PACKING_AUTO] = "auto",
    [XRD_PACKING_HALF] = "half",
    [XRD_PACKING_FULL] = "full",
};

bool xrd_value_packing(const char *s, xrd_packing *out)
{
    if (!strcmp(s, "half"))
        *out = XRD_PACKING_HALF;
    else if (!strcmp(s, "full"))
        *out = XRD_PACKING_FULL;
    else
        return false;
    return true;
}

const char *xrd_value_packing_name(xrd_packing p)
{
    if ((int)p < 0 || (size_t)p >= sizeof packing_names / sizeof packing_names[0])
        return NULL;
    return packing_names[p];
}

static const char *const zoom_mode_names[] = {
    [XRD_ZOOM_SCREEN] = "screen",
    [XRD_ZOOM_MAGNIFY] = "magnify",
};

bool xrd_value_zoom_mode(const char *s, xrd_zoom_mode *out)
{
    for (size_t i = 0; i < sizeof zoom_mode_names / sizeof zoom_mode_names[0]; i++)
        if (!strcmp(s, zoom_mode_names[i])) {
            *out = (xrd_zoom_mode)i;
            return true;
        }
    return false;
}

const char *xrd_value_zoom_mode_name(xrd_zoom_mode m)
{
    if ((int)m < 0 || (size_t)m >= sizeof zoom_mode_names / sizeof zoom_mode_names[0])
        return NULL;
    return zoom_mode_names[m];
}

bool xrd_value_yesno(const char *s, bool *out)
{
    if (!strcmp(s, "yes"))
        *out = true;
    else if (!strcmp(s, "no"))
        *out = false;
    else
        return false;
    return true;
}
