#define _POSIX_C_SOURCE 200809L /* nanosleep, O_CLOEXEC, O_NOFOLLOW */

#include "driver/xrdriver.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

const char *xrd_xrdriver_dir(void)
{
    const char *d = getenv(XRD_XRDRIVER_DIR_ENV);
    return d && d[0] ? d : XRD_XRDRIVER_DIR_DEFAULT;
}

int xrd_xrdriver_path(const char *file, char *buf, size_t len)
{
    int n = snprintf(buf, len, "%s/%s", xrd_xrdriver_dir(), file);
    return n < 0 || (size_t)n >= len ? -1 : 0;
}

/* "true" or "false" into *out; anything else leaves it and returns false. */
static bool parse_bool(const char *v, bool *out)
{
    if (!strcmp(v, "true"))
        *out = true;
    else if (!strcmp(v, "false"))
        *out = false;
    else
        return false;
    return true;
}

void xrd_xrdriver_parse_state(const char *text, xrd_xrdriver_state *st)
{
    memset(st, 0, sizeof *st);
    bool have_sup = false, have_en = false;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        char line[128];
        /* Longer lines (device_license JSON) hold no key read here. */
        if (n < sizeof line) {
            memcpy(line, p, n);
            line[n] = '\0';
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                const char *key = line, *val = eq + 1;
                if (!strcmp(key, "heartbeat")) {
                    char *end;
                    errno = 0;
                    long long hb = strtoll(val, &end, 10);
                    if (end != val && *end == '\0' && errno == 0) {
                        st->heartbeat = hb;
                        st->have_heartbeat = true;
                    }
                } else if (!strcmp(key, "sbs_mode_supported")) {
                    have_sup = parse_bool(val, &st->sbs_supported) || have_sup;
                } else if (!strcmp(key, "sbs_mode_enabled")) {
                    have_en = parse_bool(val, &st->sbs_enabled) || have_en;
                }
            }
        }
        p += n;
        if (*p == '\n')
            p++;
    }
    st->have_sbs = have_sup && have_en;
}

int xrd_xrdriver_read_state(const char *path, xrd_xrdriver_state *st)
{
    char buf[8192];
    size_t got = 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        if (attempt > 0) {
            struct timespec ts = {0, 20 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
        FILE *f = fopen(path, "r");
        if (!f)
            return -1;
        got = fread(buf, 1, sizeof buf - 1, f);
        int bad = ferror(f);
        int e = errno;
        fclose(f);
        if (bad) {
            errno = e ? e : EIO;
            return -1;
        }
        if (got > 0)
            break;
    }
    buf[got] = '\0';
    xrd_xrdriver_parse_state(buf, st);
    return 0;
}

bool xrd_xrdriver_fresh(const xrd_xrdriver_state *st, long long now)
{
    if (!st->have_heartbeat)
        return false;
    long long age = now - st->heartbeat;
    return age <= XRD_XRDRIVER_FRESH_S && age >= -XRD_XRDRIVER_FRESH_S;
}

const char *xrd_xrdriver_sbs_request(bool enable)
{
    return enable ? "sbs_mode=enable\n" : "sbs_mode=disable\n";
}

int xrd_xrdriver_request(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;
    size_t len = strlen(text);
    ssize_t w;
    do
        w = write(fd, text, len);
    while (w < 0 && errno == EINTR);
    int e = w < 0 ? errno : 0;
    if (close(fd) != 0 && e == 0)
        e = errno;
    if (e == 0 && (size_t)w != len)
        e = EIO;
    if (e != 0) {
        errno = e;
        return -1;
    }
    return 0;
}
