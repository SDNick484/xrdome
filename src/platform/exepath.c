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

bool xrd_exepath_dirname(const char *path, char *buf, size_t len)
{
    if (!path || !buf || len == 0)
        return false;
    buf[0] = '\0';
    const char *slash = strrchr(path, '/');
    if (!slash)
        return false;
    size_t n = (size_t)(slash - path);
    if (n == 0)
        n = 1; /* "/xrdome" -> "/" */
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
    char raw[PATH_MAX];
#if defined(__APPLE__)
    char tmp[PATH_MAX];
    uint32_t sz = (uint32_t)sizeof tmp;
    if (_NSGetExecutablePath(tmp, &sz) != 0)
        return false;
    /* The path may hold "..", "." or symlinks: realpath gives the real file. */
    if (!realpath(tmp, raw))
        return false;
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", raw, sizeof raw - 1);
    if (n <= 0)
        return false;
    raw[n] = '\0';
#else
    return false; /* unknown */
#endif
    return xrd_exepath_dirname(raw, buf, len);
}

bool xrd_exepath_is_exec(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

/* True when dir is one of the ':'-separated entries of path_env (a trailing
 * slash on an entry does not matter). */
static bool dir_in_path(const char *dir, const char *path_env)
{
    if (!path_env)
        return false;
    size_t dl = strlen(dir);
    while (dl > 1 && dir[dl - 1] == '/')
        dl--;
    const char *p = path_env;
    while (*p) {
        const char *end = strchr(p, ':');
        size_t el = end ? (size_t)(end - p) : strlen(p);
        while (el > 1 && p[el - 1] == '/')
            el--;
        if (el == dl && !strncmp(p, dir, dl))
            return true;
        if (!end)
            break;
        p = end + 1;
    }
    return false;
}

bool xrd_exepath_pick_ffprobe(const char *configured, const char *exe_dir, const char *path_env,
                              bool (*is_exec)(const char *path), char *out, size_t outlen)
{
    if (!out || outlen == 0)
        return false;
    out[0] = '\0';
    int n;
    if (configured && configured[0]) {
        n = snprintf(out, outlen, "%s", configured);
    } else {
        char cand[PATH_MAX];
        if (exe_dir && exe_dir[0] && !dir_in_path(exe_dir, path_env) && is_exec &&
            snprintf(cand, sizeof cand, "%s%sffprobe", exe_dir,
                     strcmp(exe_dir, "/") ? "/" : "") < (int)sizeof cand &&
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
    char dir[PATH_MAX];
    if (!xrd_exepath_dir(dir, sizeof dir))
        dir[0] = '\0';
    if (!xrd_exepath_pick_ffprobe(NULL, dir, getenv("PATH"), xrd_exepath_is_exec, out, outlen) &&
        outlen > 0)
        snprintf(out, outlen, "ffprobe"); /* too long for out: the bare name */
}
