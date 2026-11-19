#include "platform/platform.h"

#include <string.h>

xrd_platform xrd_platform_current(void)
{
#if defined(__APPLE__)
    return XRD_PLATFORM_MACOS;
#elif defined(__linux__)
    return XRD_PLATFORM_LINUX;
#else
    return XRD_PLATFORM_OTHER;
#endif
}

const char *xrd_platform_name(xrd_platform p)
{
    switch (p) {
    case XRD_PLATFORM_LINUX:
        return "linux";
    case XRD_PLATFORM_MACOS:
        return "macos";
    default:
        return "other";
    }
}

const char *xrd_platform_default_hwdec(xrd_platform p)
{
    switch (p) {
    case XRD_PLATFORM_LINUX:
        return "vaapi";
    case XRD_PLATFORM_MACOS:
        return "videotoolbox";
    default:
        return "auto-safe";
    }
}

bool xrd_platform_uses_x11(xrd_platform p)
{
    return p == XRD_PLATFORM_LINUX || p == XRD_PLATFORM_OTHER;
}

bool xrd_platform_gl_forward_compat(xrd_platform p)
{
    return p == XRD_PLATFORM_MACOS;
}

bool xrd_platform_force_fullscreen(xrd_platform p)
{
    return p == XRD_PLATFORM_MACOS;
}

bool xrd_platform_has_xrdriver(xrd_platform p)
{
    return p == XRD_PLATFORM_LINUX;
}

bool xrd_hwdec_is_zero_copy(const char *current)
{
    if (!current || current[0] == '\0' || strcmp(current, "no") == 0)
        return false;
    size_t n = strlen(current);
    static const char copy[] = "-copy";
    size_t c = sizeof copy - 1;
    return !(n >= c && strcmp(current + n - c, copy) == 0);
}

bool xrd_hwdec_name_valid(const char *name)
{
    if (!name)
        return false;
    size_t n = strlen(name);
    if (n == 0 || n > 31)
        return false;
    for (size_t i = 0; i < n; i++) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == ','))
            return false;
    }
    return true;
}
