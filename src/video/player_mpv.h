#ifndef XRD_VIDEO_PLAYER_MPV_H
#define XRD_VIDEO_PLAYER_MPV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "render/projection.h"

typedef struct xrd_player xrd_player;
typedef void *(*xrd_gl_get_proc)(void *ctx, const char *name);

/* Needs a current GL 3.3 core context, which must stay current for every call
 * below. x11_display is the X11 Display* the context lives on. Starts loading
 * file; a load failure is reported later by xrd_player_update. Returns NULL
 * and fills err on failure. */
xrd_player *xrd_player_open(const char *file, xrd_gl_get_proc get_proc, void *x11_display,
                            char *err, size_t errlen);

/* mpv tuning, applied before mpv_initialize. profile: an mpv profile name,
 * "default" for none, NULL for "fast". opts: opt_count "KEY=VALUE" strings,
 * applied after the profile and xrdome's own fixed options, so they win.
 * log_level: the least severe mpv log level printed to stderr as
 * "mpv: [<prefix>] <text>" lines (see xrd_mpv_log_level_valid); NULL for
 * "warn", "no" for none. */
typedef struct {
    const char *profile;
    const char *const *opts;
    int opt_count;
    const char *log_level;
} xrd_player_options;

/* As xrd_player_open; opt may be NULL (profile "fast", no extra options).
 * An mpv option or profile that mpv rejects fails the open, err naming it. */
xrd_player *xrd_player_open_ex(const char *file, xrd_gl_get_proc get_proc, void *x11_display,
                               const xrd_player_options *opt, char *err, size_t errlen);
void        xrd_player_free(xrd_player *p);
/* What the one-time aspect warning expects of the video: the starting layout
 * and the content FOV (--content-fov). Call before the first xrd_player_update;
 * defaults are sbs-lr and 180 x 180. */
void        xrd_player_set_content(xrd_player *p, xrd_layout layout, float hfov_deg, float vfov_deg);
/* Call once per loop. Creates or resizes the FBO when video size is known, renders a new
   frame if one is due. Returns <0 on fatal error (err text via xrd_player_error).
   After rendering into its FBO it binds framebuffer 0 again. */
int         xrd_player_update(xrd_player *p);
/* Call after each buffer swap (mpv_render_context_report_swap). */
void        xrd_player_report_swap(xrd_player *p);
unsigned    xrd_player_texture(const xrd_player *p);     /* 0 until the first frame */
void        xrd_player_video_size(const xrd_player *p, int *w, int *h);
const char *xrd_player_hwdec(xrd_player *p);             /* mpv "hwdec-current", "" if unknown */
bool        xrd_player_finished(const xrd_player *p);    /* end of file or load failure */
const char *xrd_player_error(const xrd_player *p);
void        xrd_player_toggle_pause(xrd_player *p);
void        xrd_player_pause(xrd_player *p);              /* pause (no toggle) */
void        xrd_player_seek(xrd_player *p, double seconds);
/* Development (--frames): at end of file keep showing the last frame instead
 * of finishing, so a frame count does not depend on decode speed. */
void        xrd_player_hold_at_end(xrd_player *p);

/* pure helpers, unit tested */
/* True when 0 < w,h <= gl_max; otherwise false with a message naming the
 * dimensions and the limit in err (err may be NULL). */
bool xrd_video_size_fits(int w, int h, int gl_max, char *err, size_t errlen);
/* True when the frame's aspect fits a video covering hfov_deg x vfov_deg: for
 * side-by-side layouts the per-eye aspect (w/2)/h, for mono w/h, must be
 * within 5 percent of hfov/vfov. False for non-positive sizes. */
bool xrd_video_aspect_matches(int w, int h, xrd_layout layout, float hfov_deg, float vfov_deg);
/* Splits "KEY=VALUE" at the first '='. The value may contain '=' and be empty.
 * False for no '=', an empty key, or a key or value that does not fit. */
bool xrd_mpv_opt_split(const char *s, char *key, size_t keylen, char *val, size_t vallen);
/* True for an mpv log level xrdome accepts: no, fatal, error, warn, info, v,
 * debug. False for NULL. */
bool xrd_mpv_log_level_valid(const char *level);
/* Writes one mpv log message to f as "mpv: [<prefix>] <line>\n" per line of
 * text, dropping empty lines and trailing CR/LF, so every line written starts
 * with "mpv: ". */
void xrd_mpv_log_print(FILE *f, const char *prefix, const char *text);
/* True for an exactly 2:1 frame (two square eyes side by side). */
bool xrd_video_is_sbs_2to1(int w, int h);

#endif
