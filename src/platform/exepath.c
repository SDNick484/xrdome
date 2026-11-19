#define _POSIX_C_SOURCE 200809L

#include "platform/exepath.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

/* Long enough for any path Windows hands out (the manifest makes xrdome long-path aware). */
#define EXE_PATH_MAX 32768

static bool is_sep(xrd_platform p, char c)
{
    return c == '/' || (p == XRD_PLATFORM_WINDOWS && c == '\\');
}

bool xrd_exepath_dirname(xrd_platform p, const char *path, char *buf, size_t len)
{
    if (!path || !buf || len == 0)
        return false;
    buf[0] = '\0';
    const char *slash = NULL;
    for (const char *c = path; *c; c++)
        if (is_sep(p, *c))
            slash = c;
    if (!slash)
        return false;
    size_t n = (size_t)(slash - path);
    if (n == 0 || (p == XRD_PLATFORM_WINDOWS && n == 2 && path[1] == ':'))
        n++; /* "/xrdome" -> "/", "C:\xrdome.exe" -> "C:\": the root keeps its slash */
    if (n >= len)
        return false;
    memcpy(buf, path, n);
    buf[n] = '\0';
    return true;
}

bool xrd_exepath_dir(char *buf, size_t len)
{
    if (!buf || len == 0)
        return false;
    buf[0] = '\0';
#if defined(_WIN32)
    wchar_t wide[EXE_PATH_MAX];
    char raw[EXE_PATH_MAX];
    DWORD wn = GetModuleFileNameW(NULL, wide, EXE_PATH_MAX);
    if (wn == 0 || wn >= EXE_PATH_MAX) /* failed, or truncated */
        return false;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, raw, (int)sizeof raw, NULL, NULL) <= 0)
        return false;
#elif defined(__APPLE__)
    char raw[PATH_MAX];
    char tmp[PATH_MAX];
    uint32_t sz = (uint32_t)sizeof tmp;
    if (_NSGetExecutablePath(tmp, &sz) != 0)
        return false;
    /* The path may hold "..", "." or symlinks: realpath gives the real file. */
    if (!realpath(tmp, raw))
        return false;
#elif defined(__linux__)
    char raw[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", raw, sizeof raw - 1);
    if (n <= 0)
        return false;
    raw[n] = '\0';
#else
    return false; /* unknown */
#endif
    return xrd_exepath_dirname(xrd_platform_current(), raw, buf, len);
}

bool xrd_exepath_is_exec(const char *path)
{
    struct stat st;
#ifdef _WIN32 /* no execute bit: a regular file is as good as it gets (access X_OK is not defined) */
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#else
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
#endif
}

/* The same directory name under the platform's rules: Windows ignores case (ASCII only here)
 * and takes either slash. */
static bool same_char(xrd_platform p, char a, char b)
{
    if (is_sep(p, a) || is_sep(p, b))
        return is_sep(p, a) && is_sep(p, b);
    if (p == XRD_PLATFORM_WINDOWS && a >= 'A' && a <= 'Z')
        a = (char)(a - 'A' + 'a');
    if (p == XRD_PLATFORM_WINDOWS && b >= 'A' && b <= 'Z')
        b = (char)(b - 'A' + 'a');
    return a == b;
}

/* True when dir is one of the entries of path_env (':' separated; Windows: ';', and an entry
 * may be quoted). A trailing slash on either side does not matter. */
static bool dir_in_path(xrd_platform p, const char *dir, const char *path_env)
{
    if (!path_env)
        return false;
    const char listsep = p == XRD_PLATFORM_WINDOWS ? ';' : ':';
    size_t dl = strlen(dir);
    while (dl > 1 && is_sep(p, dir[dl - 1]))
        dl--;
    const char *e = path_env;
    while (*e) {
        const char *end = strchr(e, listsep);
        size_t el = end ? (size_t)(end - e) : strlen(e);
        const char *ent = e;
        if (p == XRD_PLATFORM_WINDOWS && el >= 2 && ent[0] == '"' && ent[el - 1] == '"') {
            ent++;
            el -= 2;
        }
        while (el > 1 && is_sep(p, ent[el - 1]))
            el--;
        if (el == dl) {
            size_t i = 0;
            while (i < dl && same_char(p, ent[i], dir[i]))
                i++;
            if (i == dl)
                return true;
        }
        if (!end)
            break;
        e = end + 1;
    }
    return false;
}

bool xrd_exepath_pick_ffprobe(xrd_platform p, const char *configured, const char *exe_dir,
                              const char *path_env, bool (*is_exec)(const char *path), char *out,
                              size_t outlen)
{
    if (!out || outlen == 0)
        return false;
    out[0] = '\0';
    int n;
    if (configured && configured[0]) {
        n = snprintf(out, outlen, "%s", configured);
    } else {
        const bool win = p == XRD_PLATFORM_WINDOWS;
        char cand[EXE_PATH_MAX];
        if (exe_dir && exe_dir[0] && !dir_in_path(p, exe_dir, path_env) && is_exec &&
            snprintf(cand, sizeof cand, "%s%s%s", exe_dir,
                     is_sep(p, exe_dir[strlen(exe_dir) - 1]) ? "" : (win ? "\\" : "/"),
                     win ? "ffprobe.exe" : "ffprobe") < (int)sizeof cand &&
            is_exec(cand))
            n = snprintf(out, outlen, "%s", cand);
        else
            n = snprintf(out, outlen, "ffprobe");
    }
    if (n < 0 || (size_t)n >= outlen) {
        out[0] = '\0';
        return false;
    }
    return true;
}

void xrd_exepath_default_ffprobe(char *out, size_t outlen)
{
    char dir[EXE_PATH_MAX];
    if (!xrd_exepath_dir(dir, sizeof dir))
        dir[0] = '\0';
    if (!xrd_exepath_pick_ffprobe(xrd_platform_current(), NULL, dir, getenv("PATH"),
                                  xrd_exepath_is_exec, out, outlen) &&
        outlen > 0)
        snprintf(out, outlen, "ffprobe"); /* too long for out: the bare name */
}
