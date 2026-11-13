#define _GNU_SOURCE /* strtof */

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
