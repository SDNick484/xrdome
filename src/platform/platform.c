#define _POSIX_C_SOURCE 200809L /* mkdir */

#include "platform/platform.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <knownfolders.h>
#include <shlobj.h>
#endif

xrd_platform xrd_platform_current(void)
{
#if defined(__APPLE__)
    return XRD_PLATFORM_MACOS;
#elif defined(_WIN32)
    return XRD_PLATFORM_WINDOWS;
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
    case XRD_PLATFORM_WINDOWS:
        return "windows";
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
    case XRD_PLATFORM_WINDOWS:
        return "d3d11va-copy";
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
    return p == XRD_PLATFORM_MACOS || p == XRD_PLATFORM_WINDOWS;
}

bool xrd_platform_has_xrdriver(xrd_platform p)
{
    return p == XRD_PLATFORM_LINUX;
}

const char *xrd_platform_hwdec_note(xrd_platform p)
{
    return p == XRD_PLATFORM_WINDOWS ? "Windows decodes through a copy by default "
                                       "(d3d11va-copy: native WGL has no zero-copy path)"
                                     : "";
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

/* ---- per-user paths --------------------------------------------------- */

bool xrd_platform_path_absolute(xrd_platform p, const char *path)
{
    if (!path || path[0] == '\0')
        return false;
    if (path[0] == '/')
        return true;
    if (p != XRD_PLATFORM_WINDOWS)
        return false;
    if (path[0] == '\\')
        return true;
    char d = path[0];
    return ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z')) && path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
}

int xrd_platform_user_path(xrd_platform p, xrd_user_path_kind kind, const char *xdg,
                           const char *home, const char *known, char *buf, size_t len)
{
    const char *leaf = kind == XRD_USER_CONFIG ? "config.ini" : "files";
    int n;
    if (xrd_platform_path_absolute(p, xdg))
        n = snprintf(buf, len, "%s/xrdome/%s", xdg, leaf);
    else if (p == XRD_PLATFORM_WINDOWS && known && known[0])
        n = snprintf(buf, len, "%s\\xrdome\\%s", known, leaf);
    else if (p != XRD_PLATFORM_WINDOWS && home && home[0])
        n = snprintf(buf, len, "%s/%s/xrdome/%s", home,
                     kind == XRD_USER_CONFIG ? ".config" : ".local/state", leaf);
    else
        return -1;
    return (n < 0 || (size_t)n >= len) ? -1 : 0;
}

#ifdef _WIN32
/* The known folder (Roaming or Local AppData) as UTF-8. */
static bool known_folder(xrd_user_path_kind kind, char *buf, size_t len)
{
    PWSTR w = NULL;
    REFKNOWNFOLDERID id = kind == XRD_USER_CONFIG ? &FOLDERID_RoamingAppData
                                                  : &FOLDERID_LocalAppData;
    bool ok = false;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, NULL, &w)) && len <= INT_MAX)
        ok = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)len, NULL, NULL) > 0;
    CoTaskMemFree(w);
    return ok;
}
#endif

int xrd_user_path(xrd_user_path_kind kind, char *buf, size_t len)
{
    const char *xdg = getenv(kind == XRD_USER_CONFIG ? "XDG_CONFIG_HOME" : "XDG_STATE_HOME");
    const char *known = NULL;
#ifdef _WIN32
    char kbuf[1024];
    if (!xrd_platform_path_absolute(XRD_PLATFORM_WINDOWS, xdg) &&
        known_folder(kind, kbuf, sizeof kbuf))
        known = kbuf;
#endif
    return xrd_platform_user_path(xrd_platform_current(), kind, xdg, getenv("HOME"), known,
                                  buf, len);
}

/* ---- files -------------------------------------------------------------- */

int xrd_mkdir(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0700);
#endif
}

#ifdef _WIN32
/* UTF-8 to a malloc'd wide string; NULL (errno set) on failure. */
static wchar_t *widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (!w || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) != n) {
        free(w);
        errno = EINVAL;
        return NULL;
    }
    return w;
}
#endif

int xrd_replace_file(const char *from, const char *to)
{
#ifdef _WIN32
    wchar_t *wf = widen(from), *wt = wf ? widen(to) : NULL;
    int rc = -1;
    if (wf && wt) {
        if (MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            rc = 0;
        else
            errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : EIO;
    }
    free(wf);
    free(wt);
    return rc;
#else
    return rename(from, to);
#endif
}
