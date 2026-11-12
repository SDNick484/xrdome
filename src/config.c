#define _GNU_SOURCE /* getopt_long, strtof */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOV_MIN 10.f
#define FOV_MAX 120.f
#define ZOOM_MIN 0.25f
#define ZOOM_MAX 4.f

static const struct { const char *name; float fov_diag; } builtin_devices[] = {
    {"viture-pro", 43.0f},
    {"xreal-air2pro", 45.0f},
};

static void set_err(char *err, size_t errlen, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void set_err(char *err, size_t errlen, const char *fmt, ...)
{
    if (!err || errlen == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

/* Copies src into dst[dstlen]; false (dst untouched) if it does not fit. */
static bool copy_str(char *dst, size_t dstlen, const char *src)
{
    size_t n = strlen(src);
    if (n >= dstlen)
        return false;
    memcpy(dst, src, n + 1);
    return true;
}

static bool parse_float(const char *s, float *out)
{
    char *end;
    errno = 0;
    float v = strtof(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE || !isfinite(v))
        return false;
    *out = v;
    return true;
}

/* Parses and range-checks a float setting. what names the key or flag. */
static int parse_ranged(const char *what, const char *s, float lo, float hi, float *out,
                        char *err, size_t errlen)
{
    float v;
    if (!parse_float(s, &v)) {
        set_err(err, errlen, "%s: '%s' is not a number", what, s);
        return -1;
    }
    if (v < lo || v > hi) {
        set_err(err, errlen, "%s: %s is out of range (%g to %g)", what, s, (double)lo, (double)hi);
        return -1;
    }
    *out = v;
    return 0;
}

static int set_string(const char *what, char *dst, size_t dstlen, const char *src,
                      char *err, size_t errlen)
{
    if (!copy_str(dst, dstlen, src)) {
        set_err(err, errlen, "%s: value too long (max %zu characters)", what, dstlen - 1);
        return -1;
    }
    return 0;
}

static int parse_layout(const char *s, xrd_layout *out, char *err, size_t errlen)
{
    if (!strcmp(s, "sbs-lr"))
        *out = XRD_LAYOUT_SBS_LR;
    else if (!strcmp(s, "sbs-rl"))
        *out = XRD_LAYOUT_SBS_RL;
    else if (!strcmp(s, "mono"))
        *out = XRD_LAYOUT_MONO;
    else {
        set_err(err, errlen, "unknown layout '%s' (expected sbs-lr, sbs-rl or mono)", s);
        return -1;
    }
    return 0;
}

void xrd_config_defaults(xrd_config *c)
{
    memset(c, 0, sizeof *c);
    copy_str(c->pose, sizeof c->pose, "udp-opentrack:port=4242");
    copy_str(c->output, sizeof c->output, "auto");
    copy_str(c->device, sizeof c->device, "viture-pro");
    c->fov_diag = 43.0f;
    c->zoom = 1.0f;
    c->layout = XRD_LAYOUT_SBS_LR;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

/* Reads the ini. *device_found is set when [device.<c->device>] exists. */
static int ini_read(xrd_config *c, const char *path, bool *device_found, char *err, size_t errlen)
{
    *device_found = false;
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT)
            return 0;
        set_err(err, errlen, "%s: %s", path, strerror(errno));
        return -1;
    }

    char want[64];
    snprintf(want, sizeof want, "device.%s", c->device);

    enum { SEC_NONE, SEC_GENERAL, SEC_DEVICE, SEC_OTHER } sec = SEC_NONE;
    char line[8192];
    int lineno = 0, rc = 0;
    char msg[256];

    while (rc == 0 && fgets(line, sizeof line, f)) {
        lineno++;
        if (!strchr(line, '\n') && !feof(f)) {
            set_err(err, errlen, "%s:%d: line too long", path, lineno);
            rc = -1;
            break;
        }
        char *s = trim(line);
        if (*s == '\0' || *s == '#')
            continue;

        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close || close[1] != '\0') {
                set_err(err, errlen, "%s:%d: malformed section header", path, lineno);
                rc = -1;
                break;
            }
            *close = '\0';
            const char *name = trim(s + 1);
            if (!strcmp(name, "general"))
                sec = SEC_GENERAL;
            else if (!strcmp(name, want)) {
                sec = SEC_DEVICE;
                *device_found = true;
            } else
                sec = SEC_OTHER;
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) {
            set_err(err, errlen, "%s:%d: expected 'key = value'", path, lineno);
            rc = -1;
            break;
        }
        *eq = '\0';
        const char *key = trim(s);
        const char *val = trim(eq + 1);

        if (sec == SEC_NONE) {
            set_err(err, errlen, "%s:%d: key '%s' outside any section", path, lineno, key);
            rc = -1;
        } else if (sec == SEC_OTHER) {
            continue; /* another device's section */
        } else if (sec == SEC_GENERAL && !strcmp(key, "pose")) {
            rc = set_string("pose", c->pose, sizeof c->pose, val, msg, sizeof msg);
        } else if (sec == SEC_GENERAL && !strcmp(key, "output")) {
            rc = set_string("output", c->output, sizeof c->output, val, msg, sizeof msg);
        } else if (sec == SEC_DEVICE && !strcmp(key, "fov_diag")) {
            rc = parse_ranged("fov_diag", val, FOV_MIN, FOV_MAX, &c->fov_diag, msg, sizeof msg);
        } else if (sec == SEC_DEVICE && !strcmp(key, "zoom")) {
            rc = parse_ranged("zoom", val, ZOOM_MIN, ZOOM_MAX, &c->zoom, msg, sizeof msg);
        } else {
            snprintf(msg, sizeof msg, "unknown key '%s'", key);
            rc = -1;
        }
        if (rc != 0)
            set_err(err, errlen, "%s:%d: %s", path, lineno, msg);
    }
    fclose(f);
    return rc;
}

int xrd_config_load_ini(xrd_config *c, const char *path, char *err, size_t errlen)
{
    bool found;
    return ini_read(c, path, &found, err, errlen);
}

int xrd_config_parse_args(xrd_config *c, int argc, char **argv, char *err, size_t errlen)
{
    enum { OPT_PROJ = 256, OPT_LAYOUT, OPT_POSE, OPT_OUTPUT, OPT_DEVICE, OPT_ZOOM,
           OPT_HWDEC, OPT_WINDOWED, OPT_TIMING };
    static const struct option longopts[] = {
        {"proj", required_argument, NULL, OPT_PROJ},
        {"layout", required_argument, NULL, OPT_LAYOUT},
        {"pose", required_argument, NULL, OPT_POSE},
        {"output", required_argument, NULL, OPT_OUTPUT},
        {"device", required_argument, NULL, OPT_DEVICE},
        {"zoom", required_argument, NULL, OPT_ZOOM},
        {"require-hwdec", no_argument, NULL, OPT_HWDEC},
        {"windowed", no_argument, NULL, OPT_WINDOWED},
        {"timing-log", required_argument, NULL, OPT_TIMING},
        {NULL, 0, NULL, 0},
    };

    /* glibc re-initialises (including its argv permutation state) only for 0. */
    optind = 0;
    opterr = 0;

    int opt;
    while ((opt = getopt_long(argc, argv, ":", longopts, NULL)) != -1) {
        int rc = 0;
        switch (opt) {
        case OPT_PROJ:
            if (strcmp(optarg, "hequirect")) {
                set_err(err, errlen, "unsupported projection '%s' (supported: hequirect)", optarg);
                rc = -1;
            }
            break;
        case OPT_LAYOUT:
            rc = parse_layout(optarg, &c->layout, err, errlen);
            break;
        case OPT_POSE:
            rc = set_string("--pose", c->pose, sizeof c->pose, optarg, err, errlen);
            break;
        case OPT_OUTPUT:
            rc = set_string("--output", c->output, sizeof c->output, optarg, err, errlen);
            break;
        case OPT_DEVICE:
            rc = set_string("--device", c->device, sizeof c->device, optarg, err, errlen);
            break;
        case OPT_ZOOM:
            rc = parse_ranged("--zoom", optarg, ZOOM_MIN, ZOOM_MAX, &c->zoom, err, errlen);
            break;
        case OPT_HWDEC:
            c->require_hwdec = true;
            break;
        case OPT_WINDOWED:
            c->windowed = true;
            break;
        case OPT_TIMING:
            rc = set_string("--timing-log", c->timing_log, sizeof c->timing_log, optarg, err, errlen);
            break;
        case ':':
            set_err(err, errlen, "option '%s' requires an argument", argv[optind - 1]);
            rc = -1;
            break;
        default:
            set_err(err, errlen, "unknown option '%s'", argv[optind - 1]);
            rc = -1;
            break;
        }
        if (rc != 0)
            return -1;
    }

    if (optind >= argc) {
        set_err(err, errlen, "missing input file");
        return -1;
    }
    if (optind + 1 < argc) {
        set_err(err, errlen, "unexpected extra argument '%s'", argv[optind + 1]);
        return -1;
    }
    return set_string("input file", c->file, sizeof c->file, argv[optind], err, errlen);
}

int xrd_config_load(xrd_config *c, const char *ini_path, int argc, char **argv,
                    char *err, size_t errlen)
{
    /* Pass 1: learn --device. */
    xrd_config_defaults(c);
    if (xrd_config_parse_args(c, argc, argv, err, errlen) != 0)
        return -1;
    char device[sizeof c->device];
    memcpy(device, c->device, sizeof device);

    /* Pass 2: fresh defaults for that device, built-in FOV, then the ini. */
    xrd_config_defaults(c);
    memcpy(c->device, device, sizeof device);
    bool builtin = false;
    for (size_t i = 0; i < sizeof builtin_devices / sizeof builtin_devices[0]; i++) {
        if (!strcmp(device, builtin_devices[i].name)) {
            c->fov_diag = builtin_devices[i].fov_diag;
            builtin = true;
        }
    }

    bool found = false;
    if (ini_path && ini_read(c, ini_path, &found, err, errlen) != 0)
        return -1;
    if (!builtin && !found) {
        set_err(err, errlen,
                "unknown device '%s' (built in: viture-pro, xreal-air2pro; or add a [device.%s] section)",
                device, device);
        return -1;
    }

    /* Pass 3: flags win over the ini. */
    return xrd_config_parse_args(c, argc, argv, err, errlen);
}
