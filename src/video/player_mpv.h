#ifndef XRD_VIDEO_PLAYER_MPV_H
#define XRD_VIDEO_PLAYER_MPV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "render/projection.h"
#include "render/renderer_gl.h" /* XRD_MIP_MAX_LEVEL */

typedef struct xrd_player xrd_player;
typedef void *(*xrd_gl_get_proc)(void *ctx, const char *name);

/* Needs a current GL 3.3 core context, which must stay current for every call
 * below. x11_display is the X11 Display* the context lives on. Starts loading
 * file; a load failure is reported later by xrd_player_update. Returns NULL
 * and fills err on failure. */
xrd_player *xrd_player_open(const char *file, xrd_gl_get_proc get_proc, void *x11_display,
                            char *err, size_t errlen);

/* Format of the video texture mpv renders into. XRD_FBO_RGBA8 (GL_RGBA8) is
 * the zero value, so a zero-initialised xrd_player_options, a NULL one and
 * xrd_player_open all get 8 bits. XRD_FBO_RGB10 is GL_RGB10_A2 (10 bits per
 * colour channel, same 4 bytes per texel as RGBA8); when the GPU cannot render
 * to it the player falls back to GL_RGBA8 with a note. */
typedef enum { XRD_FBO_RGBA8, XRD_FBO_RGB10 } xrd_fbo_format;

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
    xrd_fbo_format fbo_format; /* video texture format; 0 = XRD_FBO_RGBA8 */
    bool deband;               /* mpv "deband" at open; --mpv-opt deband=... still wins */
    /* > 0: opts carries "start=<resume_at>" for a resume. When the file has
     * loaded and its duration says the position is no longer usable (past the
     * end, or in the last part), the player seeks back to 0 and, for a
     * position past the end, prints one "xrdome: note:" line. */
    double resume_at;
    /* --mpv-scale: an mpv scaler name, set as mpv's scale, with keepaspect=no
     * so mpv's picture fills a texture of any size (xrd_player_set_texture_size).
     * NULL: neither is set (mpv draws into the coded size only). opts still win:
     * keepaspect is set before them, and scale is not set when they set it.
     * A name mpv rejects fails the open, err naming it, after mpv's own reason
     * is logged. */
    const char *scale;
} xrd_player_options;

/* As xrd_player_open; opt may be NULL (profile "fast", no extra options,
 * RGBA8 video texture, as xrd_player_open).
 * An mpv option or profile that mpv rejects fails the open, err naming it. */
xrd_player *xrd_player_open_ex(const char *file, xrd_gl_get_proc get_proc, void *x11_display,
                               const xrd_player_options *opt, char *err, size_t errlen);
void        xrd_player_free(xrd_player *p);
/* What the one-time aspect warning expects of the video: the layout and the
 * content FOV (--content-fov). Defaults are sbs-lr and 180 x 180. The warning
 * is checked when the video size becomes known and when it is turned on
 * (xrd_player_set_aspect_warning), against the values set then; a later call
 * alone does not check again. */
void        xrd_player_set_content(xrd_player *p, xrd_layout layout, float hfov_deg, float vfov_deg);
/* The aspect warning (spec 5.12, xrd_aspect_warning_text) is printed only
 * while this is on (hequirect), at most once per player. Default on. Turning
 * it on with the video size known checks at once. */
void        xrd_player_set_aspect_warning(xrd_player *p, bool on);
/* Subtitles drawn by mpv into the video texture: shown only while on (flat
 * 2D) and mpv's own sub-visibility at open (default yes; --mpv-opt
 * sub-visibility=no) allows them. */
void        xrd_player_set_sub_visible(xrd_player *p, bool on);
/* mpv "time-pos" and "duration" in seconds; -1 when unknown. */
double      xrd_player_position(xrd_player *p);
double      xrd_player_duration(xrd_player *p);
const char *xrd_player_codec(xrd_player *p);   /* mpv "video-codec", "" if unknown */
double      xrd_player_fps(xrd_player *p);     /* mpv "container-fps", 0 if unknown */
/* Call once per loop. Creates or resizes the FBO when video size is known, renders a new
   frame if one is due. Returns <0 on fatal error (err text via xrd_player_error).
   After rendering into its FBO it binds framebuffer 0 again. */
int         xrd_player_update(xrd_player *p);
/* Call after each buffer swap (mpv_render_context_report_swap). */
void        xrd_player_report_swap(xrd_player *p);
unsigned    xrd_player_texture(const xrd_player *p);     /* 0 until the first frame */
/* Bits per colour channel of the video texture actually allocated: 10 for
 * GL_RGB10_A2, 8 for GL_RGBA8 (also after a fallback), 0 before the FBO exists. */
int         xrd_player_texture_bits(const xrd_player *p);
/* The coded frame size after the rotation mpv applies (xrd_video_texture_size),
 * whatever display aspect the container declares. 0 x 0 until mpv reports it. */
void        xrd_player_video_size(const xrd_player *p, int *w, int *h);
/* The video texture's size: the coded size (xrd_player_video_size), or the
 * size asked for with xrd_player_set_texture_size. mpv's picture fills the
 * texture exactly. 0 x 0 before the FBO exists. */
void        xrd_player_texture_size(const xrd_player *p, int *w, int *h);
/* Asks for a w x h video texture, into which mpv scales each frame with its
 * own scaler (only meaningful with xrd_player_options.scale set: without
 * keepaspect=no mpv letterboxes a texture of another shape). 0 x 0 returns
 * to the coded size, which is also the default. Before the video size is known
 * the request waits for it; a later change of the coded size keeps a
 * requested size. When the texture holds a frame, mpv draws the current frame
 * again at the new size before this returns, so xrd_player_texture never
 * shows a frame that is missing or half drawn. Returns <0 on failure (text
 * via xrd_player_error). w and h must fit the GPU's maximum texture size. */
int         xrd_player_set_texture_size(xrd_player *p, int w, int h);
/* While want is true, the chain (levels 0..XRD_MIP_MAX_LEVEL) is regenerated
 * right after each video frame mpv renders, and the texture's min filter is
 * GL_LINEAR_MIPMAP_LINEAR. While false: GL_LINEAR and no generation. Turning
 * it on while a frame is in the texture generates the chain at once, so a
 * paused video can be sampled trilinearly. Default false. */
void        xrd_player_set_mipmaps(xrd_player *p, bool want);
/* True when the texture's chain matches its current level 0 (generated for the
 * frame now in the texture, at the current size). Only then may the eye pass
 * sample it with xrd_draw_params.trilinear. */
bool        xrd_player_mipmapped(const xrd_player *p);
/* mpv's "deband" property: set (true if mpv accepted it) and read (the live value,
 * so --mpv-opt deband=yes shows). */
bool        xrd_player_set_deband(xrd_player *p, bool on);
bool        xrd_player_deband(xrd_player *p);
const char *xrd_player_hwdec(xrd_player *p);             /* mpv "hwdec-current", "" if unknown */
const char *xrd_player_scale(xrd_player *p);             /* mpv "scale", "" if unknown */
/* mpv's gpu-dumb-mode is "yes" (software GL): mpv then ignores scale and
 * enlarges bilinearly. */
bool        xrd_player_dumb_mode(xrd_player *p);
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
 * side-by-side layouts the per-eye aspect (w/2)/h, for top-bottom w/(h/2),
 * for mono w/h, must be within 5 percent of hfov/vfov. False for non-positive
 * sizes. */
bool xrd_video_aspect_matches(int w, int h, xrd_layout layout, float hfov_deg, float vfov_deg);
/* The aspect warning of spec 5.12 without the "xrdome: warning: " prefix, one
 * line with no newline: "video is WxH, which is not the aspect of a
 * side-by-side|top-bottom|mono VR video covering HxV degrees. ...". Returns
 * the length, or -1 (buf emptied) when it does not fit. */
int xrd_aspect_warning_text(char *buf, size_t len, int w, int h, xrd_layout layout,
                            float hfov_deg, float vfov_deg);
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
/* The GL internal format for f (GL_RGB10_A2 or GL_RGBA8), and in *depth the
 * bits per channel to tell mpv (MPV_RENDER_PARAM_DEPTH): 10 or 8. */
int xrd_fbo_format_gl(xrd_fbo_format f, int *depth);
/* Name of a video texture by xrd_player_texture_bits: "rgb10_a2" for 10,
 * "rgba8" for 8, "none" otherwise. */
const char *xrd_texture_bits_name(int bits);
/* True for an exactly 2:1 frame (two square eyes side by side). */
bool xrd_video_is_sbs_2to1(int w, int h);
/* What to do on MPV_EVENT_VIDEO_RECONFIG. have_size: the frame size could be
 * read. vo_configured: mpv's "vo-configured", or -1 when it could not be read.
 * With no size, an output that is not configured is waited for: mpv sends a
 * reconfig before the output is up (start of playback) and after it has gone
 * (end of file), and another follows once it is configured. */
typedef enum { XRD_RECONFIG_RESIZE, XRD_RECONFIG_WAIT, XRD_RECONFIG_FAIL } xrd_reconfig_step;
xrd_reconfig_step xrd_reconfig_step_for(bool have_size, int vo_configured);
/* The video texture size for a w x h frame (mpv's video-out-params w and h)
 * that mpv shows rotated clockwise by rotate degrees (video-out-params rotate):
 * w x h, or h x w for a quarter or three-quarter turn (any multiple of 90,
 * negative or above 360 included). */
void xrd_video_texture_size(int w, int h, int rotate, int *tw, int *th);

/* Whether the video texture's mipmap chain matches its level 0. The player
 * drives it from three events and generates the chain whenever an event
 * returns true; valid is then true until the next frame or resize. */
typedef struct {
    bool want;  /* mipmaps requested (xrd_player_set_mipmaps) */
    bool valid; /* the chain was generated from the frame now in level 0 */
} xrd_mip_state;
/* Level 0 was reallocated (new video size): the chain no longer matches. */
void xrd_mip_on_resize(xrd_mip_state *s);
/* mpv rendered a new frame into level 0. True: generate the chain now. */
bool xrd_mip_on_frame(xrd_mip_state *s);
/* Mipmaps were requested or dropped; have_frame: level 0 holds a frame at the
 * current size. True: generate the chain now (want turned on over a frame). */
bool xrd_mip_on_want(xrd_mip_state *s, bool want, bool have_frame);

#endif
