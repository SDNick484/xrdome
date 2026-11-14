#ifndef XRD_CONFIG_H
#define XRD_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "render/projection.h"
#include "render/renderer_gl.h"
#include "store/filestate.h"
#include "video/player_mpv.h"

typedef struct {
    char file[4096];
    char pose[128];        /* default "udp-opentrack:port=4242" */
    char output[64];       /* default "auto" */
    char device[32];       /* default "viture-pro" */
    float fov_diag;        /* from [device.<name>], default 43.0 */
    float zoom;            /* default 1.0 */
    xrd_layout layout;     /* default XRD_LAYOUT_SBS_LR; valid when !layout_auto */
    float content_hfov;    /* --content-fov HxV (degrees the video covers), default 180 */
    float content_vfov;    /* default 180 */
    bool require_hwdec;
    bool windowed;         /* development: skip the 3840x1080 check, 1280x360 window */
    char timing_log[4096]; /* empty = off */
    bool sync_after_swap;  /* diagnostic: glFinish() right after the swap */
    char mpv_profile[32];  /* --mpv-profile; default "fast"; "default" = apply no profile */
    char mpv_opts[16][256]; /* --mpv-opt KEY=VALUE, in order */
    int mpv_opt_count;
    char mpv_log[8];       /* --mpv-log LEVEL; default "warn"; "no" = none */
    float convergence;     /* --convergence DEG, -5 .. 5, default 0 */
    xrd_filter filter;     /* --filter bilinear|bicubic, default bicubic */
    xrd_fbo_format fbo_format; /* --fbo-format rgb10|rgba8, default rgb10 */
    bool dither;           /* --dither on|off, default on */
    unsigned explicit_flags; /* XRD_SET_ZOOM|LAYOUT|CONTENT_FOV|CONVERGENCE|PROJ|PACKING|ZOOM_MODE given on the command line */
    bool no_memory;        /* --no-memory */
    bool forget;           /* --forget */
    bool proj_auto;        /* --proj auto (default true) */
    xrd_proj proj;         /* valid when !proj_auto */
    bool layout_auto;      /* --layout auto (default true); layout valid when false */
    xrd_packing packing;   /* --packing auto|half|full, default AUTO */
    xrd_zoom_mode zoom_mode; /* --zoom-mode, default XRD_ZOOM_SCREEN */
    float zoom_default;    /* VR180 zoom from device and ini, before --zoom */
    float flat_zoom;       /* [device.X] flat_zoom, default 1.5 */
    bool screen_locked;    /* --screen fixed|locked, [general] screen */
    bool mipmap;           /* --mipmap on|off, default on */
    char ffprobe[256];     /* [general] ffprobe, default "ffprobe" */
    /* development only (smoke test), not documented: */
    int frames;            /* --frames N: exit 0 after N presented frames
                              once a video frame has been rendered; 0 = off */
    char dump_frame[4096]; /* --dump-frame PATH: PPM of the last frame; needs --frames */
} xrd_config;

void xrd_config_defaults(xrd_config *c);

/* Writes $XDG_CONFIG_HOME/xrdome/config.ini into buf, or
 * $HOME/.config/xrdome/config.ini when XDG_CONFIG_HOME is unset, empty or not
 * absolute. Returns 0, or -1 when HOME is also unset or empty, or the path
 * does not fit in len bytes. */
int xrd_config_default_path(char *buf, size_t len);

/* Reads [general] and [device.<c->device>] from the ini at path.
 * Returns 0 on success or when the file does not exist, -1 on a bad value
 * (message in err, naming the key). */
int xrd_config_load_ini(xrd_config *c, const char *path, char *err, size_t errlen);

/* Applies command-line flags on top of *c. Returns 0, or -1 with a message in
 * err. Safe to call repeatedly in one process. */
int xrd_config_parse_args(xrd_config *c, int argc, char **argv, char *err, size_t errlen);

/* Full load: defaults, built-in device FOV, ini (may be NULL), then flags, so
 * flags win. Fails if the device is neither built in nor in the ini. */
int xrd_config_load(xrd_config *c, const char *ini_path, int argc, char **argv,
                    char *err, size_t errlen);

#endif
