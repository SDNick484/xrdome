#define _GNU_SOURCE /* getopt_long, strtof */

#include "config.h"
#include "values.h"
#include "video/player_mpv.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOV_MIN 10.f
#define FOV_MAX 120.f

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

/* Parses and range-checks a float setting. what names the key or flag. */
static int parse_ranged(const char *what, const char *s, float lo, float hi, float *out,
                        char *err, size_t errlen)
{
    switch (xrd_value_ranged(s, lo, hi, out)) {
    case XRD_VALUE_OK:
        return 0;
    case XRD_VALUE_RANGE:
        set_err(err, errlen, "%s: %s is out of range (%g to %g)", what, s, (double)lo, (double)hi);
        return -1;
    default:
        set_err(err, errlen, "%s: '%s' is not a number", what, s);
        return -1;
    }
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
    if (!xrd_value_layout(s, out)) {
        set_err(err, errlen, "unknown layout '%s' (expected auto, sbs-lr, sbs-rl, tb-lr, tb-rl or mono)", s);
        return -1;
    }
    return 0;
}

/* "fixed" or "locked" into *locked. what names the flag or key. */
static int parse_screen(const char *what, const char *s, bool *locked, char *err, size_t errlen)
{
    if (!strcmp(s, "fixed"))
        *locked = false;
    else if (!strcmp(s, "locked"))
        *locked = true;
    else {
        set_err(err, errlen, "%s: unknown value '%s' (expected fixed or locked)", what, s);
        return -1;
    }
    return 0;
}

/* "HxV" in degrees, each within XRD_CONTENT_FOV_MIN..MAX. Leaves *h and *v
 * untouched on error. */
static int parse_content_fov(const char *s, float *h, float *v, char *err, size_t errlen)
{
    switch (xrd_value_content_fov(s, h, v)) {
    case XRD_VALUE_OK:
        return 0;
    case XRD_VALUE_RANGE:
        set_err(err, errlen, "--content-fov: %s is out of range (each value %g to %g degrees)",
                s, (double)XRD_CONTENT_FOV_MIN, (double)XRD_CONTENT_FOV_MAX);
        return -1;
    default:
        set_err(err, errlen, "--content-fov: '%s' is not HxV in degrees (e.g. 120x90)", s);
        return -1;
    }
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
    c->content_hfov = 180.0f;
    c->content_vfov = 180.0f;
    copy_str(c->mpv_profile, sizeof c->mpv_profile, "fast");
    copy_str(c->mpv_log, sizeof c->mpv_log, "warn");
    c->convergence = 0.0f;
    c->filter = XRD_FILTER_BICUBIC;
    c->fbo_format = XRD_FBO_RGB10;
    c->dither = true;
    c->proj_auto = true;
    c->proj = XRD_PROJ_HEQUIRECT;
    c->layout_auto = true;
    c->packing = XRD_PACKING_AUTO;
    c->zoom_mode = XRD_ZOOM_SCREEN;
    c->zoom_default = 1.0f;
    c->flat_zoom = 1.5f;
    c->screen_locked = false;
    c->mipmap = true;
    copy_str(c->ffprobe, sizeof c->ffprobe, "ffprobe");
}

int xrd_config_default_path(char *buf, size_t len)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int n;
    if (xdg && xdg[0] == '/')
        n = snprintf(buf, len, "%s/xrdome/config.ini", xdg);
    else if (home && home[0] != '\0')
        n = snprintf(buf, len, "%s/.config/xrdome/config.ini", home);
    else
        return -1;
    return (n < 0 || (size_t)n >= len) ? -1 : 0;
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
    char msg[256] = "";

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
            snprintf(msg, sizeof msg, "key '%s' outside any section", key);
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
            rc = parse_ranged("zoom", val, XRD_ZOOM_MIN, XRD_ZOOM_MAX, &c->zoom, msg, sizeof msg);
            if (rc == 0)
                c->zoom_default = c->zoom;
        } else if (sec == SEC_DEVICE && !strcmp(key, "flat_zoom")) {
            rc = parse_ranged("flat_zoom", val, XRD_ZOOM_MIN, XRD_ZOOM_MAX, &c->flat_zoom, msg,
                              sizeof msg);
        } else if (sec == SEC_GENERAL && !strcmp(key, "screen")) {
            rc = parse_screen("screen", val, &c->screen_locked, msg, sizeof msg);
        } else if (sec == SEC_GENERAL && !strcmp(key, "ffprobe")) {
            rc = set_string("ffprobe", c->ffprobe, sizeof c->ffprobe, val, msg, sizeof msg);
        } else {
            snprintf(msg, sizeof msg, "unknown key '%s'", key);
            rc = -1;
        }
        if (rc != 0)
            set_err(err, errlen, "%s:%d: %s", path, lineno, msg);
    }
    if (rc == 0 && ferror(f)) {
        set_err(err, errlen, "%s: read error: %s", path, strerror(errno));
        rc = -1;
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
           OPT_CONTENT_FOV, OPT_HWDEC, OPT_WINDOWED, OPT_TIMING, OPT_SYNC, OPT_FRAMES, OPT_DUMP,
           OPT_MPV_PROFILE, OPT_MPV_OPT, OPT_MPV_LOG, OPT_CONVERGENCE, OPT_FILTER,
           OPT_FBO_FORMAT, OPT_DITHER, OPT_NO_MEMORY, OPT_FORGET,
           OPT_PACKING, OPT_ZOOM_MODE, OPT_SCREEN, OPT_MIPMAP };
    static const struct option longopts[] = {
        {"proj", required_argument, NULL, OPT_PROJ},
        {"layout", required_argument, NULL, OPT_LAYOUT},
        {"pose", required_argument, NULL, OPT_POSE},
        {"output", required_argument, NULL, OPT_OUTPUT},
        {"device", required_argument, NULL, OPT_DEVICE},
        {"zoom", required_argument, NULL, OPT_ZOOM},
        {"content-fov", required_argument, NULL, OPT_CONTENT_FOV},
        {"require-hwdec", no_argument, NULL, OPT_HWDEC},
        {"windowed", no_argument, NULL, OPT_WINDOWED},
        {"timing-log", required_argument, NULL, OPT_TIMING},
        {"sync-after-swap", no_argument, NULL, OPT_SYNC},
        {"frames", required_argument, NULL, OPT_FRAMES},
        {"dump-frame", required_argument, NULL, OPT_DUMP},
        {"mpv-profile", required_argument, NULL, OPT_MPV_PROFILE},
        {"mpv-opt", required_argument, NULL, OPT_MPV_OPT},
        {"mpv-log", required_argument, NULL, OPT_MPV_LOG},
        {"convergence", required_argument, NULL, OPT_CONVERGENCE},
        {"filter", required_argument, NULL, OPT_FILTER},
        {"fbo-format", required_argument, NULL, OPT_FBO_FORMAT},
        {"dither", required_argument, NULL, OPT_DITHER},
        {"no-memory", no_argument, NULL, OPT_NO_MEMORY},
        {"forget", no_argument, NULL, OPT_FORGET},
        {"packing", required_argument, NULL, OPT_PACKING},
        {"zoom-mode", required_argument, NULL, OPT_ZOOM_MODE},
        {"screen", required_argument, NULL, OPT_SCREEN},
        {"mipmap", required_argument, NULL, OPT_MIPMAP},
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
            if (!strcmp(optarg, "auto")) {
                c->proj_auto = true;
                c->explicit_flags &= ~XRD_SET_PROJ;
            } else if (xrd_value_proj(optarg, &c->proj)) {
                c->proj_auto = false;
                c->explicit_flags |= XRD_SET_PROJ;
            } else {
                set_err(err, errlen, "--proj: unknown projection '%s' (expected auto, hequirect "
                        "or flat)", optarg);
                rc = -1;
            }
            break;
        case OPT_LAYOUT:
            if (!strcmp(optarg, "auto")) {
                c->layout_auto = true;
                c->explicit_flags &= ~XRD_SET_LAYOUT;
            } else {
                rc = parse_layout(optarg, &c->layout, err, errlen);
                if (rc == 0) {
                    c->layout_auto = false;
                    c->explicit_flags |= XRD_SET_LAYOUT;
                }
            }
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
            rc = parse_ranged("--zoom", optarg, XRD_ZOOM_MIN, XRD_ZOOM_MAX, &c->zoom, err, errlen);
            if (rc == 0)
                c->explicit_flags |= XRD_SET_ZOOM;
            break;
        case OPT_CONTENT_FOV:
            rc = parse_content_fov(optarg, &c->content_hfov, &c->content_vfov, err, errlen);
            if (rc == 0)
                c->explicit_flags |= XRD_SET_CONTENT_FOV;
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
        case OPT_SYNC:
            c->sync_after_swap = true;
            break;
        case OPT_FRAMES: {
            char *end;
            errno = 0;
            long v = strtol(optarg, &end, 10);
            if (end == optarg || *end != '\0' || errno == ERANGE || v < 1 || v > INT_MAX) {
                set_err(err, errlen, "--frames: '%s' is not a positive integer", optarg);
                rc = -1;
            } else
                c->frames = (int)v;
            break;
        }
        case OPT_DUMP:
            rc = set_string("--dump-frame", c->dump_frame, sizeof c->dump_frame, optarg, err, errlen);
            break;
        case OPT_MPV_PROFILE:
            if (optarg[0] == '\0') {
                set_err(err, errlen, "--mpv-profile: empty profile name");
                rc = -1;
            } else
                rc = set_string("--mpv-profile", c->mpv_profile, sizeof c->mpv_profile, optarg,
                                err, errlen);
            break;
        case OPT_MPV_OPT: {
            char k[256], v[256];
            if (c->mpv_opt_count >= (int)(sizeof c->mpv_opts / sizeof c->mpv_opts[0])) {
                set_err(err, errlen, "--mpv-opt '%s': at most %zu --mpv-opt options are allowed",
                        optarg, sizeof c->mpv_opts / sizeof c->mpv_opts[0]);
                rc = -1;
            } else if (strlen(optarg) >= sizeof c->mpv_opts[0]) {
                set_err(err, errlen, "--mpv-opt: option too long (max %zu characters)",
                        sizeof c->mpv_opts[0] - 1);
                rc = -1;
            } else if (!xrd_mpv_opt_split(optarg, k, sizeof k, v, sizeof v)) {
                set_err(err, errlen, "--mpv-opt: '%s' is not KEY=VALUE with a non-empty KEY", optarg);
                rc = -1;
            } else
                copy_str(c->mpv_opts[c->mpv_opt_count++], sizeof c->mpv_opts[0], optarg);
            break;
        }
        case OPT_MPV_LOG:
            if (!xrd_mpv_log_level_valid(optarg)) {
                set_err(err, errlen, "--mpv-log: unknown level '%s' "
                        "(expected no, fatal, error, warn, info, v or debug)", optarg);
                rc = -1;
            } else
                copy_str(c->mpv_log, sizeof c->mpv_log, optarg);
            break;
        case OPT_CONVERGENCE:
            rc = parse_ranged("--convergence", optarg, -XRD_CONVERGENCE_MAX, XRD_CONVERGENCE_MAX,
                              &c->convergence, err, errlen);
            if (rc == 0)
                c->explicit_flags |= XRD_SET_CONVERGENCE;
            break;
        case OPT_FILTER:
            if (!strcmp(optarg, "bilinear"))
                c->filter = XRD_FILTER_BILINEAR;
            else if (!strcmp(optarg, "bicubic"))
                c->filter = XRD_FILTER_BICUBIC;
            else {
                set_err(err, errlen, "--filter: unknown filter '%s' (expected bilinear or bicubic)",
                        optarg);
                rc = -1;
            }
            break;
        case OPT_FBO_FORMAT:
            if (!strcmp(optarg, "rgb10"))
                c->fbo_format = XRD_FBO_RGB10;
            else if (!strcmp(optarg, "rgba8"))
                c->fbo_format = XRD_FBO_RGBA8;
            else {
                set_err(err, errlen, "--fbo-format: unknown format '%s' (expected rgb10 or rgba8)",
                        optarg);
                rc = -1;
            }
            break;
        case OPT_DITHER:
            if (!strcmp(optarg, "on"))
                c->dither = true;
            else if (!strcmp(optarg, "off"))
                c->dither = false;
            else {
                set_err(err, errlen, "--dither: unknown value '%s' (expected on or off)", optarg);
                rc = -1;
            }
            break;
        case OPT_NO_MEMORY:
            c->no_memory = true;
            break;
        case OPT_FORGET:
            c->forget = true;
            break;
        case OPT_PACKING:
            if (!strcmp(optarg, "auto")) {
                c->packing = XRD_PACKING_AUTO;
                c->explicit_flags &= ~XRD_SET_PACKING;
            } else if (xrd_value_packing(optarg, &c->packing)) {
                c->explicit_flags |= XRD_SET_PACKING;
            } else {
                set_err(err, errlen, "--packing: unknown value '%s' (expected auto, half or full)",
                        optarg);
                rc = -1;
            }
            break;
        case OPT_ZOOM_MODE:
            if (xrd_value_zoom_mode(optarg, &c->zoom_mode))
                c->explicit_flags |= XRD_SET_ZOOM_MODE;
            else {
                set_err(err, errlen, "--zoom-mode: unknown value '%s' (expected screen or magnify)",
                        optarg);
                rc = -1;
            }
            break;
        case OPT_SCREEN:
            rc = parse_screen("--screen", optarg, &c->screen_locked, err, errlen);
            break;
        case OPT_MIPMAP:
            if (!strcmp(optarg, "on"))
                c->mipmap = true;
            else if (!strcmp(optarg, "off"))
                c->mipmap = false;
            else {
                set_err(err, errlen, "--mipmap: unknown value '%s' (expected on or off)", optarg);
                rc = -1;
            }
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

    if (c->dump_frame[0] && c->frames == 0) {
        set_err(err, errlen, "--dump-frame needs --frames");
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

    if (!builtin)
        c->fov_diag = 0.f; /* sentinel: the ini must supply it (range check forbids 0) */

    bool found = false;
    if (ini_path && ini_read(c, ini_path, &found, err, errlen) != 0)
        return -1;
    if (!builtin && !found) {
        set_err(err, errlen,
                "unknown device '%s' (built in: viture-pro, xreal-air2pro; or add a [device.%s] section)",
                device, device);
        return -1;
    }
    if (!builtin && c->fov_diag == 0.f) {
        set_err(err, errlen, "device '%s': [device.%s] is missing required key fov_diag",
                device, device);
        return -1;
    }

    /* Pass 3: flags win over the ini. */
    return xrd_config_parse_args(c, argc, argv, err, errlen);
}
