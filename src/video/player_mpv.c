#include "video/player_mpv.h"

#include "store/filestate.h"

#include <epoxy/gl.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xrd_player {
    mpv_handle *mpv;
    mpv_render_context *rctx;
    char file[4096];
    GLuint fbo, tex;
    int w, h;          /* FBO size, 0 until mpv reports the video size */
    xrd_fbo_format fbo_format; /* requested; becomes RGBA8 after a fallback */
    int fbo_internal;  /* GL internal format of tex, 0 before the FBO exists */
    int fbo_depth;     /* bits per channel of tex: 10 or 8, 0 before the FBO exists */
    bool have_frame;   /* a frame has been rendered into the FBO */
    xrd_mip_state mip; /* decides when levels 1..XRD_MIP_MAX_LEVEL are generated */
    /* Ground truth for xrd_player_mipmapped, kept apart from mip so that
     * --mipmap-check tests the decisions: the serial of what level 0 holds
     * (a new one for every frame mpv renders and every reallocation) and of
     * what the chain was generated from (0 = none). */
    unsigned long frame_serial, mip_serial;
    bool finished;
    bool warned_aspect;
    bool aspect_warning; /* xrd_player_set_aspect_warning */
    bool sub_pref;       /* mpv's sub-visibility at open */
    char codec[128];
    xrd_layout layout; /* what the aspect warning expects; see xrd_player_set_content */
    float hfov_deg, vfov_deg;
    char hwdec[64];
    double resume_at;  /* xrd_player_options.resume_at, cleared once checked */
    char err[512];
};

static void set_err(char *err, size_t errlen, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void check_aspect(xrd_player *p);
static void check_resume(xrd_player *p);

static void set_err(char *err, size_t errlen, const char *fmt, ...)
{
    if (!err || errlen == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

bool xrd_video_size_fits(int w, int h, int gl_max, char *err, size_t errlen)
{
    if (w <= 0 || h <= 0) {
        set_err(err, errlen, "invalid video size %dx%d", w, h);
        return false;
    }
    if (w > gl_max || h > gl_max) {
        set_err(err, errlen, "video size %dx%d exceeds the GPU's maximum texture size %d",
                w, h, gl_max);
        return false;
    }
    return true;
}

bool xrd_video_aspect_matches(int w, int h, xrd_layout layout, float hfov_deg, float vfov_deg)
{
    if (w <= 0 || h <= 0 || !(hfov_deg > 0.f) || !(vfov_deg > 0.f))
        return false;
    double have = xrd_layout_is_sbs(layout) ? (w / 2.0) / h
                : xrd_layout_is_tb(layout)  ? w / (h / 2.0)
                                            : (double)w / h;
    double want = (double)hfov_deg / vfov_deg;
    double rel = have / want - 1.0;
    return rel >= -0.05 && rel <= 0.05;
}

int xrd_aspect_warning_text(char *buf, size_t len, int w, int h, xrd_layout layout,
                            float hfov_deg, float vfov_deg)
{
    const char *kind = xrd_layout_is_sbs(layout) ? "side-by-side"
                     : xrd_layout_is_tb(layout)  ? "top-bottom"
                                                 : "mono";
    int n = snprintf(buf, len,
                     "video is %dx%d, which is not the aspect of a %s VR video covering %gx%g "
                     "degrees. If it is an ordinary flat video, press P or use --proj flat. If it "
                     "is VR video covering less than 180 degrees, set --content-fov. Playing it "
                     "as given.",
                     w, h, kind, (double)hfov_deg, (double)vfov_deg);
    if (n < 0 || (size_t)n >= len) {
        if (len > 0)
            buf[0] = '\0';
        return -1;
    }
    return n;
}

bool xrd_video_is_sbs_2to1(int w, int h)
{
    return w > 0 && h > 0 && w == 2 * h;
}

xrd_reconfig_step xrd_reconfig_step_for(bool have_size, int vo_configured)
{
    if (have_size)
        return XRD_RECONFIG_RESIZE;
    return vo_configured == 0 ? XRD_RECONFIG_WAIT : XRD_RECONFIG_FAIL;
}

void xrd_video_texture_size(int w, int h, int rotate, int *tw, int *th)
{
    bool quarter = ((rotate % 180) + 180) % 180 == 90;
    *tw = quarter ? h : w;
    *th = quarter ? w : h;
}

void xrd_mip_on_resize(xrd_mip_state *s)
{
    s->valid = false;
}

bool xrd_mip_on_frame(xrd_mip_state *s)
{
    s->valid = s->want;
    return s->want;
}

bool xrd_mip_on_want(xrd_mip_state *s, bool want, bool have_frame)
{
    bool was = s->want;
    s->want = want;
    if (!want) {
        s->valid = false;
        return false;
    }
    if (was || !have_frame)
        return false; /* already kept valid per frame, or nothing to generate from yet */
    s->valid = true;
    return true;
}

int xrd_fbo_format_gl(xrd_fbo_format f, int *depth)
{
    if (f == XRD_FBO_RGB10) {
        *depth = 10;
        return GL_RGB10_A2;
    }
    *depth = 8;
    return GL_RGBA8;
}

const char *xrd_texture_bits_name(int bits)
{
    return bits == 10 ? "rgb10_a2" : bits == 8 ? "rgba8" : "none";
}

bool xrd_mpv_log_level_valid(const char *level)
{
    static const char *const levels[] = {"no", "fatal", "error", "warn", "info", "v", "debug"};
    if (!level)
        return false;
    for (size_t i = 0; i < sizeof levels / sizeof levels[0]; i++)
        if (!strcmp(level, levels[i]))
            return true;
    return false;
}

void xrd_mpv_log_print(FILE *f, const char *prefix, const char *text)
{
    const char *s = text;
    while (*s) {
        size_t n = strcspn(s, "\n");
        size_t len = n;
        while (len > 0 && s[len - 1] == '\r')
            len--;
        if (len > 0)
            fprintf(f, "mpv: [%s] %.*s\n", prefix, (int)len, s);
        s += n;
        if (*s == '\n')
            s++;
    }
}

/* Prints any log messages queued in mpv, dropping other events. Used on the
 * failure path of open, where the update loop will never run. */
static void drain_log(mpv_handle *mpv)
{
    for (;;) {
        mpv_event *ev = mpv_wait_event(mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE)
            return;
        if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            const mpv_event_log_message *m = ev->data;
            xrd_mpv_log_print(stderr, m->prefix, m->text);
        }
    }
}

static int set_opt(xrd_player *p, const char *name, const char *value, char *err, size_t errlen)
{
    int rc = mpv_set_option_string(p->mpv, name, value);
    if (rc < 0) {
        set_err(err, errlen, "mpv option %s=%s: %s", name, value, mpv_error_string(rc));
        return -1;
    }
    return 0;
}

bool xrd_mpv_opt_split(const char *s, char *key, size_t keylen, char *val, size_t vallen)
{
    const char *eq = strchr(s, '=');
    if (!eq || eq == s)
        return false;
    size_t kn = (size_t)(eq - s), vn = strlen(eq + 1);
    if (kn >= keylen || vn >= vallen)
        return false;
    memcpy(key, s, kn);
    key[kn] = '\0';
    memcpy(val, eq + 1, vn + 1);
    return true;
}

xrd_player *xrd_player_open(const char *file, xrd_gl_get_proc get_proc, void *x11_display,
                            char *err, size_t errlen)
{
    return xrd_player_open_ex(file, get_proc, x11_display, NULL, err, errlen);
}

xrd_player *xrd_player_open_ex(const char *file, xrd_gl_get_proc get_proc, void *x11_display,
                               const xrd_player_options *opt, char *err, size_t errlen)
{
    xrd_player *p = calloc(1, sizeof *p);
    if (!p) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    snprintf(p->file, sizeof p->file, "%s", file);
    p->layout = XRD_LAYOUT_SBS_LR;
    p->hfov_deg = 180.f;
    p->vfov_deg = 180.f;
    p->fbo_format = opt ? opt->fbo_format : XRD_FBO_RGBA8;
    p->aspect_warning = true;
    p->sub_pref = true;
    p->resume_at = opt ? opt->resume_at : 0.0;

    p->mpv = mpv_create();
    if (!p->mpv) {
        set_err(err, errlen, "mpv_create failed");
        free(p);
        return NULL;
    }
    /* mpv's own warnings and errors (e.g. why audio output failed) go to
     * stderr from the event drain. */
    const char *log_level = opt && opt->log_level ? opt->log_level : "warn";
    int lrc = mpv_request_log_messages(p->mpv, log_level);
    if (lrc < 0) {
        set_err(err, errlen, "mpv log level '%s': %s", log_level, mpv_error_string(lrc));
        goto fail;
    }
    /* The profile goes first so every explicit option below wins over it. */
    const char *profile = opt && opt->profile ? opt->profile : "fast";
    if (strcmp(profile, "default") != 0) {
        int prc = mpv_set_option_string(p->mpv, "profile", profile);
        if (prc < 0) {
            if (!strcmp(profile, "fast"))
                fprintf(stderr, "xrdome: note: mpv profile 'fast' unavailable, using mpv defaults\n");
            else {
                set_err(err, errlen, "mpv profile '%s': %s", profile, mpv_error_string(prc));
                goto fail;
            }
        }
    }
    if (set_opt(p, "vo", "libmpv", err, errlen) || set_opt(p, "hwdec", "vaapi", err, errlen) ||
        set_opt(p, "osd-level", "0", err, errlen))
        goto fail;
    if (set_opt(p, "deband", opt && opt->deband ? "yes" : "no", err, errlen))
        goto fail;
    /* Square pixels: mpv ignores every aspect the file declares, so it draws
     * the coded frame filling a texture of the coded size (spec 5.10).
     * Without it a stereo-tagged Matroska file, whose display size describes
     * one eye, is stretched (1920x1080 SBS to 1920x2160 on mpv 0.37). xrdome
     * works out the picture's shape itself. Newer mpv spells this
     * video-aspect-method=ignore and deprecates video-aspect-override=0
     * (with a warning on every file); older mpv (0.37) rejects "ignore"
     * and takes the override. Either way --mpv-opt can undo it. */
    if (mpv_set_option_string(p->mpv, "video-aspect-method", "ignore") < 0 &&
        set_opt(p, "video-aspect-override", "0", err, errlen))
        goto fail;

    /* Mesa llvmpipe (software GL, used for the headless smoke test) renders
     * mpv's default multi-pass path as all black or with zeroed chroma in
     * about half of all runs (mpv 0.37, Mesa 25.2). The single-pass dumb mode
     * renders correctly there. Hardware GL keeps mpv's default path. */
    const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
    if (gl_renderer && (strstr(gl_renderer, "llvmpipe") || strstr(gl_renderer, "softpipe"))) {
        if (set_opt(p, "gpu-dumb-mode", "yes", err, errlen))
            goto fail;
        fprintf(stderr, "note: software GL (%s), using mpv gpu-dumb-mode\n", gl_renderer);
    }

    /* User overrides last: they win over the profile and the options above. */
    for (int i = 0; opt && i < opt->opt_count; i++) {
        char key[256], val[256];
        if (!xrd_mpv_opt_split(opt->opts[i], key, sizeof key, val, sizeof val)) {
            set_err(err, errlen, "mpv option '%s' is not KEY=VALUE", opt->opts[i]);
            goto fail;
        }
        int orc = mpv_set_option_string(p->mpv, key, val);
        if (orc < 0) {
            set_err(err, errlen, "mpv option '%s': %s", key, mpv_error_string(orc));
            goto fail;
        }
    }

    int rc = mpv_initialize(p->mpv);
    if (rc < 0) {
        set_err(err, errlen, "mpv_initialize: %s", mpv_error_string(rc));
        goto fail;
    }

    /* What the user's options (or mpv's default) say about subtitles; xrdome
     * only ever hides them on top of it. */
    int subs = 1;
    if (mpv_get_property(p->mpv, "sub-visibility", MPV_FORMAT_FLAG, &subs) >= 0)
        p->sub_pref = subs != 0;

    mpv_opengl_init_params gl_init = {.get_proc_address = get_proc, .get_proc_address_ctx = NULL};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init},
        {MPV_RENDER_PARAM_X11_DISPLAY, x11_display},
        {MPV_RENDER_PARAM_INVALID, NULL},
    };
    rc = mpv_render_context_create(&p->rctx, p->mpv, params);
    if (rc < 0) {
        set_err(err, errlen, "mpv_render_context_create: %s", mpv_error_string(rc));
        goto fail;
    }

    const char *cmd[] = {"loadfile", file, NULL};
    rc = mpv_command(p->mpv, cmd);
    if (rc < 0) {
        set_err(err, errlen, "cannot play '%s': %s", file, mpv_error_string(rc));
        goto fail;
    }
    return p;

fail:
    if (p->mpv)
        drain_log(p->mpv);
    xrd_player_free(p);
    return NULL;
}

void xrd_player_set_content(xrd_player *p, xrd_layout layout, float hfov_deg, float vfov_deg)
{
    p->layout = layout;
    p->hfov_deg = hfov_deg;
    p->vfov_deg = vfov_deg;
}

void xrd_player_set_aspect_warning(xrd_player *p, bool on)
{
    bool was = p->aspect_warning;
    p->aspect_warning = on;
    if (on && !was)
        check_aspect(p);
}

void xrd_player_set_sub_visible(xrd_player *p, bool on)
{
    int flag = on && p->sub_pref;
    mpv_set_property(p->mpv, "sub-visibility", MPV_FORMAT_FLAG, &flag);
}

const char *xrd_player_codec(xrd_player *p)
{
    char *s = mpv_get_property_string(p->mpv, "video-codec");
    snprintf(p->codec, sizeof p->codec, "%s", s ? s : "");
    mpv_free(s);
    return p->codec;
}

/* The file has loaded: a resume position that its duration rules out (the file
 * was replaced by a shorter one) goes back to the start. */
static void check_resume(xrd_player *p)
{
    double at = p->resume_at;
    p->resume_at = 0.0;
    if (!(at > 0.0))
        return;
    double dur = xrd_player_duration(p);
    xrd_resume_verdict v = xrd_resume_check(at, dur);
    if (v == XRD_RESUME_OK || v == XRD_RESUME_TOO_EARLY)
        return;
    if (v == XRD_RESUME_PAST_END) {
        char a[32], b[32];
        xrd_format_position(at, a, sizeof a);
        xrd_format_position(dur, b, sizeof b);
        fprintf(stderr,
                "xrdome: note: saved position %s is past the end of this file (%s); "
                "starting from the beginning\n", a, b);
    }
    const char *cmd[] = {"seek", "0", "absolute", NULL};
    mpv_command(p->mpv, cmd);
}

double xrd_player_position(xrd_player *p)
{
    double v = 0.0;
    if (mpv_get_property(p->mpv, "time-pos", MPV_FORMAT_DOUBLE, &v) < 0 || !(v >= 0.0))
        return -1.0;
    return v;
}

double xrd_player_duration(xrd_player *p)
{
    double v = 0.0;
    if (mpv_get_property(p->mpv, "duration", MPV_FORMAT_DOUBLE, &v) < 0 || !(v > 0.0))
        return -1.0;
    return v;
}

double xrd_player_fps(xrd_player *p)
{
    double fps = 0.0;
    if (mpv_get_property(p->mpv, "container-fps", MPV_FORMAT_DOUBLE, &fps) < 0 || !(fps > 0.0))
        return 0.0;
    return fps;
}

void xrd_player_free(xrd_player *p)
{
    if (!p)
        return;
    /* The render context must go before the core (and while GL is current). */
    if (p->rctx)
        mpv_render_context_free(p->rctx);
    if (p->mpv)
        mpv_terminate_destroy(p->mpv);
    if (p->fbo)
        glDeleteFramebuffers(1, &p->fbo);
    if (p->tex)
        glDeleteTextures(1, &p->tex);
    free(p);
}

/* (Re)allocates p->tex as a w x h texture of the given internal format,
 * attaches it to p->fbo and returns the framebuffer status. */
static GLenum attach_texture(xrd_player *p, int internal, int w, int h)
{
    if (!p->tex)
        glGenTextures(1, &p->tex);
    glBindTexture(GL_TEXTURE_2D, p->tex);
    /* No data is uploaded; format and type only have to be a valid pair. */
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA,
                 internal == GL_RGB10_A2 ? GL_UNSIGNED_INT_2_10_10_10_REV : GL_UNSIGNED_BYTE,
                 NULL);
    /* GL_LINEAR until a chain has been generated for this size: levels 1 and
     * up still have the old size, so a mipmapped min filter would leave the
     * texture incomplete (it samples as black). */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, XRD_MIP_MAX_LEVEL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return status;
}

/* Prints the aspect warning once, while it is on and the size is known. */
static void check_aspect(xrd_player *p)
{
    if (!p->aspect_warning || p->warned_aspect || p->w <= 0 || p->h <= 0)
        return;
    if (xrd_video_aspect_matches(p->w, p->h, p->layout, p->hfov_deg, p->vfov_deg))
        return;
    char text[512];
    if (xrd_aspect_warning_text(text, sizeof text, p->w, p->h, p->layout, p->hfov_deg,
                                p->vfov_deg) > 0)
        fprintf(stderr, "xrdome: warning: %s\n", text);
    p->warned_aspect = true;
}

/* (Re)creates the FBO for a w x h video. Returns <0 with p->err set. */
static int ensure_fbo(xrd_player *p, int w, int h)
{
    if (w == p->w && h == p->h && p->fbo)
        return 0;

    GLint max = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    if (!xrd_video_size_fits(w, h, max, p->err, sizeof p->err))
        return -1;
    if (!p->fbo)
        glGenFramebuffers(1, &p->fbo);
    int depth;
    int internal = xrd_fbo_format_gl(p->fbo_format, &depth);
    GLenum status = attach_texture(p, internal, w, h);
    if (status != GL_FRAMEBUFFER_COMPLETE && p->fbo_format == XRD_FBO_RGB10) {
        fprintf(stderr, "xrdome: note: 10-bit video texture unavailable, using 8-bit\n");
        p->fbo_format = XRD_FBO_RGBA8; /* later resizes go straight to 8-bit */
        while (glGetError() != GL_NO_ERROR) {
            /* drop any error the 10-bit allocation raised */
        }
        internal = xrd_fbo_format_gl(p->fbo_format, &depth);
        status = attach_texture(p, internal, w, h);
    }
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        set_err(p->err, sizeof p->err, "video FBO %dx%d incomplete (0x%x)", w, h, status);
        return -1;
    }
    p->fbo_internal = internal;
    p->fbo_depth = depth;
    p->w = w;
    p->h = h;
    check_aspect(p);
    p->have_frame = false; /* new size: wait for a frame drawn at it */
    p->frame_serial++;     /* level 0 is new: no chain matches it */
    xrd_mip_on_resize(&p->mip);
    return 0;
}

/* Regenerates levels 1..XRD_MIP_MAX_LEVEL from level 0 and switches the
 * texture to trilinear minification. */
static void generate_mipmaps(xrd_player *p)
{
    glBindTexture(GL_TEXTURE_2D, p->tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    p->mip_serial = p->frame_serial;
}

static int handle_events(xrd_player *p)
{
    for (;;) {
        mpv_event *ev = mpv_wait_event(p->mpv, 0);
        switch (ev->event_id) {
        case MPV_EVENT_NONE:
            return 0;
        case MPV_EVENT_VIDEO_RECONFIG: {
            /* The frame as mpv outputs it, not dwidth/dheight (its display
             * size, which follows the container's aspect). */
            int64_t w = 0, h = 0, rot = 0;
            int rc = mpv_get_property(p->mpv, "video-out-params/w", MPV_FORMAT_INT64, &w);
            if (rc >= 0)
                rc = mpv_get_property(p->mpv, "video-out-params/h", MPV_FORMAT_INT64, &h);
            if (rc >= 0 && mpv_get_property(p->mpv, "video-out-params/rotate", MPV_FORMAT_INT64,
                                            &rot) < 0)
                rot = 0;
            if (rc < 0) {
                int configured = -1;
                if (mpv_get_property(p->mpv, "vo-configured", MPV_FORMAT_FLAG, &configured) < 0)
                    configured = -1;
                if (xrd_reconfig_step_for(false, configured) == XRD_RECONFIG_WAIT)
                    break;
                set_err(p->err, sizeof p->err, "cannot read the video size of '%s': %s",
                        p->file, mpv_error_string(rc));
                p->finished = true;
                return -1;
            }
            int tw, th;
            xrd_video_texture_size((int)w, (int)h, (int)(rot % 360), &tw, &th);
            if (ensure_fbo(p, tw, th) < 0) {
                p->finished = true;
                return -1;
            }
            break;
        }
        case MPV_EVENT_FILE_LOADED: {
            /* Checked here, before audio or video output start, so a file
             * without video fails the same way everywhere. */
            int64_t vid = 0;
            if (mpv_get_property(p->mpv, "current-tracks/video/id", MPV_FORMAT_INT64, &vid) < 0) {
                set_err(p->err, sizeof p->err, "'%s' has no video track", p->file);
                p->finished = true;
                return -1;
            }
            check_resume(p);
            break;
        }
        case MPV_EVENT_END_FILE: {
            const mpv_event_end_file *ef = ev->data;
            p->finished = true;
            if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                set_err(p->err, sizeof p->err, "cannot play '%s': %s", p->file,
                        mpv_error_string(ef->error));
                return -1;
            }
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            p->finished = true;
            break;
        case MPV_EVENT_LOG_MESSAGE: {
            const mpv_event_log_message *m = ev->data;
            xrd_mpv_log_print(stderr, m->prefix, m->text);
            break;
        }
        default:
            break;
        }
    }
}

int xrd_player_update(xrd_player *p)
{
    if (handle_events(p) < 0)
        return -1;

    uint64_t flags = mpv_render_context_update(p->rctx);
    if ((flags & MPV_RENDER_UPDATE_FRAME) && p->fbo) {
        mpv_opengl_fbo fbo = {.fbo = (int)p->fbo, .w = p->w, .h = p->h,
                              .internal_format = p->fbo_internal};
        int flip_y = 1; /* GL orientation: t = 1 is the image top */
        int depth = p->fbo_depth; /* what mpv targets (and dithers to, if enabled) */
        /* Do not wait for the frame's display time inside render: the loop
         * is paced by the swap, and a wait here would sit between the pose
         * sample and the swap. */
        int block = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_DEPTH, &depth},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
            {MPV_RENDER_PARAM_INVALID, NULL},
        };
        int rc = mpv_render_context_render(p->rctx, params);
        glBindFramebuffer(GL_FRAMEBUFFER, 0); /* mpv leaves its target bound */
        if (rc < 0) {
            set_err(p->err, sizeof p->err, "mpv render: %s", mpv_error_string(rc));
            return -1;
        }
        p->have_frame = true;
        p->frame_serial++;
        /* Right after the frame: the eye pass never sees level 0 ahead of
         * its chain. */
        if (xrd_mip_on_frame(&p->mip))
            generate_mipmaps(p);
    }
    return 0;
}

void xrd_player_report_swap(xrd_player *p)
{
    mpv_render_context_report_swap(p->rctx);
}

void xrd_player_set_mipmaps(xrd_player *p, bool want)
{
    if (xrd_mip_on_want(&p->mip, want, p->have_frame))
        generate_mipmaps(p);
    else if (!want && p->tex) {
        glBindTexture(GL_TEXTURE_2D, p->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

bool xrd_player_mipmapped(const xrd_player *p)
{
    /* A reallocation gives level 0 a new serial, so a chain of the old size
     * never counts. */
    return p->mip.want && p->have_frame && p->mip_serial == p->frame_serial;
}

unsigned xrd_player_texture(const xrd_player *p)
{
    return p->have_frame ? p->tex : 0;
}

int xrd_player_texture_bits(const xrd_player *p)
{
    return p->fbo_depth;
}

void xrd_player_video_size(const xrd_player *p, int *w, int *h)
{
    *w = p->w;
    *h = p->h;
}

bool xrd_player_set_deband(xrd_player *p, bool on)
{
    int v = on ? 1 : 0;
    return mpv_set_property(p->mpv, "deband", MPV_FORMAT_FLAG, &v) >= 0;
}

bool xrd_player_deband(xrd_player *p)
{
    int v = 0;
    if (mpv_get_property(p->mpv, "deband", MPV_FORMAT_FLAG, &v) < 0)
        return false;
    return v != 0;
}

const char *xrd_player_hwdec(xrd_player *p)
{
    char *s = mpv_get_property_string(p->mpv, "hwdec-current");
    snprintf(p->hwdec, sizeof p->hwdec, "%s", s ? s : "");
    mpv_free(s);
    return p->hwdec;
}

bool xrd_player_finished(const xrd_player *p)
{
    return p->finished;
}

const char *xrd_player_error(const xrd_player *p)
{
    return p->err;
}

void xrd_player_toggle_pause(xrd_player *p)
{
    const char *cmd[] = {"cycle", "pause", NULL};
    mpv_command_async(p->mpv, 0, cmd);
}

void xrd_player_pause(xrd_player *p)
{
    int yes = 1;
    mpv_set_property(p->mpv, "pause", MPV_FORMAT_FLAG, &yes);
}

void xrd_player_seek(xrd_player *p, double seconds)
{
    char arg[32];
    snprintf(arg, sizeof arg, "%.3f", seconds);
    const char *cmd[] = {"seek", arg, "relative", NULL};
    mpv_command_async(p->mpv, 0, cmd);
}

void xrd_player_hold_at_end(xrd_player *p)
{
    mpv_set_property_string(p->mpv, "keep-open", "always");
}
