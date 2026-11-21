/* xrdome: VR180 and flat video on XR glasses with head tracking. */
#include <SDL.h>
#include <SDL_syswm.h>
#include <errno.h>
#include <epoxy/gl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app/controls.h"
#include "app/depthclient.h"
#include "app/imagedir.h"
#include "app/session.h"
#include "config.h"
#include "detect/detect.h"
#include "detect/probe.h"
#include "display/output.h"
#include "display/sbs.h"
#include "math/quat.h"
#include "platform/platform.h"
#include "pose/pose.h"
#include "render/depthmap.h"
#include "render/projection.h"
#include "render/renderer_gl.h"
#include "store/filestate.h"
#include "track/tracker.h"
#include "util/timing.h"
#include "values.h"
#include "video/framesink.h"
#include "video/mpv_scale.h"
#include "video/player_mpv.h"

#define MAX_OUTPUTS 16
#define WINDOWED_W 1280
#define WINDOWED_H 360
#define XRD_PI_D 3.14159265358979323846
#define XRD_PRESS_GAP 3  /* --press: frames of the photo shown before each key */
#define XRD_PRESS_MAX 32

static int fail(const char *fmt, const char *arg)
{
    fprintf(stderr, "xrdome: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    return 1;
}

static void *gl_get_proc(void *ctx, const char *name)
{
    (void)ctx;
    return SDL_GL_GetProcAddress(name);
}

/* Reads the back buffer and writes it as a top-down binary PPM. */
static int dump_ppm(const char *path, int w, int h)
{
    size_t row = (size_t)w * 3;
    unsigned char *px = malloc(row * (size_t)h);
    if (!px)
        return -1;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(px);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) /* GL rows are bottom-up */
        fwrite(px + (size_t)y * row, 1, row, f);
    free(px);
    return fclose(f) == 0 ? 0 : -1;
}

/* Per-file settings memory (spec 5.4 to 5.8 of the per-file design): the
 * record found at start and what decides whether anything is written at quit. */
typedef struct {
    bool on;            /* a key and a state directory exist and --no-memory is not given */
    bool have_loaded;   /* a record was read and parsed */
    bool block_save;    /* the record is unreadable or newer: never overwrite it */
    char key[XRD_FILESTATE_KEY_MAX];
    char dir[4096];
    xrd_filestate loaded;
    xrd_file_settings start; /* the values playback starts with (xrd_session_start) */
    unsigned explicit_flags; /* xrd_session.explicit_flags */
    bool resume;             /* open at resume_at (set by detect_and_start) */
    double resume_at;        /* seconds */
} xrd_memory;

/* Everything the keys, the per-frame view and the I key read or change. */
typedef struct {
    const xrd_config *cfg;
    xrd_controls *ctl;
    xrd_tracker *trk;
    xrd_player *p;
    xrd_memory *mem;
    const xrd_timing *timing;
    xrd_detect det;          /* detection at start: the sources of proj, layout, packing */
    xrd_probe probe;         /* valid when have_probe */
    bool have_probe;
    float content_hfov, content_vfov;
    bool proj_keyed, layout_keyed, packing_keyed; /* changed by a key in this run (for I) */
    float eye_aspect;        /* (out_w / 2) / out_h of the last frame */
    int out_eye_w, out_eye_h;
    xrd_view_geom geom;      /* of the last frame drawn */
    bool mip_want;           /* mipmaps requested from the player */
    bool trilinear;          /* the last draw sampled trilinearly */
    int max_tex;             /* GL_MAX_TEXTURE_SIZE */
    xrd_size_debounce scale_db; /* --mpv-scale: the texture size asked of the player */
    const char *scale_why;   /* --mpv-scale: why mpv is not enlarging, NULL while it is */
    unsigned long frames;    /* video frames presented */
    xrd_imagedir photos;     /* the directory argument's photos; count 0 for a file */
    size_t photo_index;      /* which of them is open */
    int nav;                 /* PageDown +1 / PageUp -1 pressed, not yet acted on */
    xrd_renderer *r;
    int mpv_major, mpv_minor; /* xrd_mpv_version_parse of mpv's version, 0.0 if unknown */
    int depth_w, depth_h;    /* the open photo's depth in the renderer; 0 x 0 for none */
    bool depth_checked;      /* its aspect was checked against the picture shown */
    const char *depth_why;   /* why the open file has no depth (D and I), NULL when it has */
    xrd_depthclient *dc;     /* the depth helper (depth serve); NULL when off or not photos */
    char depth_asked[XRD_DEPTH_PATH_MAX]; /* the open photo's absolute path while the helper
                                             makes its depth; "" otherwise */
    /* Video depth (spec 8): a film's frames to the depth helper while it is
     * flat mono with 3D on. */
    xrd_framesink *sink;     /* NULL while no frames go out */
    unsigned sink_n;         /* rings made in this run: the shm name's counter */
    bool video_failed;       /* the helper cannot take video: no more rings this run */
    uint64_t sink_ns, sink_max_ns, sink_steps; /* CPU time of the sink's steps */
    uint16_t *vdepth;        /* the newest depth map read back from the ring (322x182) */
    uint64_t vdepth_seen;    /* number of the newest map taken from the ring */
    uint64_t vdepth_taken, vdepth_age_sum, vdepth_age_max; /* maps uploaded, their age in frames */
} xrd_app;

/* F of spec 5.3 for the frame now playing: the video texture's (coded) size
 * times the probe's pixel aspect, which counts as 1:1 when the container
 * carries stereo metadata or there was no probe. Before the first frame, the
 * probe's own F (0 when unknown). */
static double frame_aspect_now(const xrd_app *a)
{
    int vw = 0, vh = 0;
    xrd_player_video_size(a->p, &vw, &vh);
    if (vw <= 0 || vh <= 0)
        return a->det.frame_aspect;
    double sn = 1.0, sd = 1.0;
    if (a->have_probe && a->probe.stereo == XRD_STEREO_NONE && a->probe.sar_num > 0 &&
        a->probe.sar_den > 0) {
        sn = a->probe.sar_num;
        sd = a->probe.sar_den;
    }
    return (double)vw * sn / ((double)vh * sd);
}

/* "layout: sbs-lr half" for flat stereo (AUTO packing resolved), otherwise
 * "layout: mono" or "layout: sbs-lr". */
static void print_layout(const xrd_app *a)
{
    const xrd_controls *c = a->ctl;
    const char *name = xrd_value_layout_name(c->layout);
    if (c->proj == XRD_PROJ_FLAT && c->layout != XRD_LAYOUT_MONO) {
        xrd_packing k = xrd_packing_resolve(c->layout, c->packing, frame_aspect_now(a));
        fprintf(stderr, "layout: %s %s\n", name, xrd_value_packing_name(k));
    } else {
        fprintf(stderr, "layout: %s\n", name);
    }
}

/* What depends on the projection and layout in the player: subtitles only in
 * flat 2D (spec 5.11), the aspect warning only for hequirect (spec 5.12). */
static void apply_format(const xrd_app *a)
{
    const xrd_controls *c = a->ctl;
    xrd_player_set_sub_visible(a->p, c->proj == XRD_PROJ_FLAT && c->layout == XRD_LAYOUT_MONO);
    xrd_player_set_content(a->p, c->layout, a->content_hfov, a->content_vfov);
    xrd_player_set_aspect_warning(a->p, c->proj == XRD_PROJ_HEQUIRECT);
}

/* --mpv-scale: the texture size for this frame (0 x 0 = the coded size) and
 * why mpv is not the one enlarging, or NULL when it is. mip_want: mipmaps
 * are in use (the picture is shrunk, or --mipmap-force). */
static const char *mpv_scale_target(const xrd_app *a, int vw, int vh, bool mip_want, int *w,
                                    int *h)
{
    const xrd_controls *c = a->ctl;
    *w = *h = 0;
    if (c->proj != XRD_PROJ_FLAT)
        return "VR180";
    if (mip_want)
        return a->cfg->mipmap_force ? "mipmaps forced" : "picture shrunk";
    int fw, fh;
    if (!xrd_mpv_scale_size(&a->geom, c->layout, vw, vh, a->out_eye_w, a->out_eye_h, a->max_tex,
                            &fw, &fh))
        return "picture not enlarged";
    *w = fw;
    *h = fh;
    return NULL;
}

/* View geometry for the current state and the eye size, and the mipmap
 * decision of spec 5.6: mipmaps while the picture is shrunk at the centre.
 * With --mpv-scale, the size of the texture mpv renders into. Returns <0 when
 * the player fails to resize (text via xrd_player_error). */
static int update_view(xrd_app *a, int out_w, int out_h)
{
    const xrd_controls *c = a->ctl;
    int vw = 0, vh = 0;
    xrd_player_video_size(a->p, &vw, &vh);
    double f = frame_aspect_now(a);
    double pa = xrd_picture_aspect(c->layout, xrd_packing_resolve(c->layout, c->packing, f), f);
    a->out_eye_w = out_w / 2;
    a->out_eye_h = out_h;
    a->eye_aspect = (out_w / 2.0f) / (float)out_h;
    xrd_view_geometry(c->proj, c->zoom_mode, a->cfg->fov_diag, a->eye_aspect, c->zoom, pa, &a->geom);
    int ew = xrd_layout_is_sbs(c->layout) ? vw / 2 : vw;
    int eh = xrd_layout_is_tb(c->layout) ? vh / 2 : vh;
    float ratio = xrd_minify_ratio(c->proj, &a->geom, ew, eh, a->out_eye_w, a->out_eye_h,
                                   a->content_hfov, a->content_vfov);
    bool want = a->cfg->mipmap_force || (a->cfg->mipmap && ratio > 1.0f);
    if (want != a->mip_want) {
        xrd_player_set_mipmaps(a->p, want);
        a->mip_want = want;
    }
    if (a->cfg->mpv_scale[0]) {
        int tw, th;
        const char *why = mpv_scale_target(a, vw, vh, want, &tw, &th);
        if (xrd_size_debounce_step(&a->scale_db, tw, th, xrd_now_ns(), XRD_MPV_SCALE_SETTLE_NS) &&
            xrd_player_set_texture_size(a->p, a->scale_db.w, a->scale_db.h) < 0)
            return -1;
        a->scale_why = a->scale_db.w > 0 ? NULL : why ? why : "settling";
    }
    a->trilinear = want && xrd_player_mipmapped(a->p);
    return 0;
}

/* The video texture: line, after the first frame and, with --mpv-scale, each
 * time the texture's size changes. */
static void print_texture(const xrd_app *a)
{
    int tw, th;
    xrd_player_texture_size(a->p, &tw, &th);
    fprintf(stderr, "video texture: %s %dx%d dither=%s mipmap=%s",
            xrd_texture_bits_name(xrd_player_texture_bits(a->p)), tw, th,
            a->cfg->dither ? "on" : "off", a->mip_want ? "on" : "off");
    if (a->cfg->mpv_scale[0])
        fprintf(stderr, " mpv-scale=%s", a->scale_db.w > 0 ? xrd_player_scale(a->p) : "unused");
    fputc('\n', stderr);
}

/* The source words of the detect: line, or "key" for a value a key changed. */
static const char *src_text(xrd_detect_src s, const char *tok, bool keyed, char *buf, size_t len)
{
    const char *w;
    if (keyed)
        return "key";
    switch (s) {
    case XRD_SRC_FLAG:     w = "flag"; break;
    case XRD_SRC_RECORD:   w = "record"; break;
    case XRD_SRC_METADATA: w = "metadata"; break;
    case XRD_SRC_NAME:     snprintf(buf, len, "file name \"%s\"", tok); return buf;
    case XRD_SRC_SHAPE:    w = "frame shape"; break;
    case XRD_SRC_AUTO:     w = "auto"; break;
    case XRD_SRC_FALLBACK: w = "fallback"; break;
    case XRD_SRC_IMAGE:    w = "image file"; break;
    default:               w = "unknown"; break;
    }
    return w;
}

/* The key list (?). */
static void print_keys(void)
{
    for (size_t i = 0; i < xrd_key_list_len; i++)
        fprintf(stderr, "keys: %s  %s\n", xrd_key_list[i].key, xrd_key_list[i].action);
}

/* "screen curve: off" or "screen curve: 1.00", the same in both zoom modes. */
static void print_curve(const xrd_controls *c, const char *prefix)
{
    if (!c->screen_curved)
        fprintf(stderr, "%sscreen curve: off\n", prefix);
    else
        fprintf(stderr, "%sscreen curve: %.2f\n", prefix, (double)c->screen_curve);
}

/* "sharpen: off" or "sharpen: 0.50". */
static void print_sharpen(float s, const char *prefix)
{
    if (s > 0.f)
        fprintf(stderr, "%ssharpen: %.2f\n", prefix, (double)s);
    else
        fprintf(stderr, "%ssharpen: off\n", prefix);
}

/* "depth: on, strength 2.0%, plane 0.50" or "depth: off ..." */
static void print_depth(const xrd_controls *c, const char *prefix)
{
    fprintf(stderr, "%sdepth: %s, strength %.1f%%, plane %.2f\n", prefix,
            c->depth_on ? "on" : "off", (double)c->depth_strength * 100.0,
            (double)c->depth_plane);
}

/* The I key (spec 5.7): one item per line. */
static void print_info(xrd_app *a)
{
    const xrd_controls *c = a->ctl;
    bool flat = c->proj == XRD_PROJ_FLAT;
    char sb[64];
    int vw = 0, vh = 0;
    xrd_player_video_size(a->p, &vw, &vh);
    const char *codec = xrd_player_codec(a->p);
    const char *hw = xrd_player_hwdec(a->p);
    double fps = xrd_player_fps(a->p);

    fprintf(stderr, "info: file: %s\n", a->cfg->file);
    if (a->photos.count > 0)
        fprintf(stderr, "info: photo: %zu of %zu\n", a->photo_index + 1, a->photos.count);
    fprintf(stderr, "info: video: %s %dx%d", codec[0] ? codec : "unknown codec", vw, vh);
    if (fps > 0.0)
        fprintf(stderr, " %.3f fps\n", fps);
    else
        fprintf(stderr, " unknown fps\n");
    fprintf(stderr, "info: hwdec: %s\n", hw[0] ? hw : "unknown");
    fprintf(stderr, "info: projection: %s (%s)\n", xrd_value_proj_name(c->proj),
            src_text(a->det.proj_src, a->det.proj_token, a->proj_keyed, sb, sizeof sb));
    fprintf(stderr, "info: layout: %s (%s)\n", xrd_value_layout_name(c->layout),
            src_text(a->det.layout_src, a->det.layout_token, a->layout_keyed, sb, sizeof sb));
    if (flat && c->layout != XRD_LAYOUT_MONO) {
        xrd_packing k = xrd_packing_resolve(c->layout, c->packing, frame_aspect_now(a));
        bool is_auto = c->packing == XRD_PACKING_AUTO;
        xrd_detect_src ks = is_auto ? XRD_SRC_AUTO : a->det.packing_src;
        fprintf(stderr, "info: packing: %s (%s)\n", xrd_value_packing_name(k),
                src_text(ks, a->det.packing_token, a->packing_keyed && !is_auto, sb, sizeof sb));
    }
    if (flat) {
        fprintf(stderr, "info: zoom: %.2f, zoom mode %s", (double)c->zoom,
                xrd_value_zoom_mode_name(c->zoom_mode));
        if (a->frames > 0)
            fprintf(stderr, ", screen %.1f x %.1f degrees\n",
                    2.0 * xrd_flat_half_angle(a->geom.half_w, xrd_controls_screen_curve(c)) *
                        180.0 / XRD_PI_D,
                    2.0 * atan((double)a->geom.half_h) * 180.0 / XRD_PI_D);
        else
            fputc('\n', stderr);
        fprintf(stderr, "info: screen: %s\n", c->screen_locked ? "head-locked" : "fixed");
        print_curve(c, "info: ");
    } else {
        fprintf(stderr, "info: zoom: %.2f\n", (double)c->zoom);
        fprintf(stderr, "info: screen: fixed (VR180)\n");
    }
    fprintf(stderr, "info: filter: %s, mipmaps %s\n",
            a->trilinear ? "trilinear" : xrd_value_filter_name(c->filter),
            a->mip_want ? "on" : "off");
    print_sharpen(c->sharpen, "info: ");
    if (a->depth_why)
        fprintf(stderr, "info: depth: none (%s)\n", a->depth_why);
    else
        print_depth(c, "info: ");
    int tw, th;
    xrd_player_texture_size(a->p, &tw, &th);
    if (!a->cfg->mpv_scale[0]) {
        fprintf(stderr, "info: mpv scale: off\n");
    } else {
        const char *name = xrd_player_scale(a->p);
        const char *dumb = xrd_player_dumb_mode(a->p) ? " (gpu-dumb-mode: mpv enlarges bilinearly)"
                                                      : "";
        if (a->scale_why)
            fprintf(stderr, "info: mpv scale: %s, not in use (%s)\n", name, a->scale_why);
        else
            fprintf(stderr, "info: mpv scale: %s, %dx%d to %dx%d%s\n", name, vw, vh, tw, th, dumb);
    }
    fprintf(stderr, "info: texture: %s %dx%d dither=%s\n",
            xrd_texture_bits_name(xrd_player_texture_bits(a->p)), tw, th,
            a->cfg->dither ? "on" : "off");
    fprintf(stderr, "info: deband: %s\n", xrd_player_deband(a->p) ? "on" : "off");
    fprintf(stderr, "info: frames: %lu presented, %u missed vsyncs\n", a->frames,
            xrd_timing_missed_vsyncs(a->timing));
}

/* Prints the zoom, filter, sharpen and depth lines for the values that differ
 * between before and after (a key press at a limit prints nothing). */
static void report_display_changes(const xrd_controls *before, const xrd_controls *after)
{
    if (after->zoom != before->zoom)
        fprintf(stderr, "zoom: %.2f\n", (double)after->zoom);
    if (after->filter != before->filter)
        fprintf(stderr, "filter: %s\n", xrd_value_filter_name(after->filter));
    if (after->sharpen != before->sharpen)
        print_sharpen(after->sharpen, "");
    if (after->depth_on != before->depth_on || after->depth_strength != before->depth_strength ||
        after->depth_plane != before->depth_plane)
        print_depth(after, "");
}

/* A depth file for the photo now open, read into the renderer: false when it
 * does not exist (nothing printed), true otherwise, with the depth: line or a
 * warning or note saying why the photo stays 2D. from: where the file came
 * from, for the depth: line. */
static bool depth_take(xrd_app *a, const char *path, const char *from)
{
    char err[256] = "";
    xrd_depthmap m;
    switch (xrd_depthmap_read(path, &m, err, sizeof err)) {
    case XRD_DEPTH_NONE:
        return false;
    case XRD_DEPTH_BAD:
        a->depth_why = "its cache file is unreadable";
        fprintf(stderr, "xrdome: warning: depth %s: %s; showing the photo in 2D\n", path, err);
        return true;
    case XRD_DEPTH_OK:
        break;
    }
    const char *file = a->cfg->file;
    int orient = xrd_exif_orientation(file);
    if (!xrd_depth_orientation_ok(orient, a->mpv_major, a->mpv_minor)) {
        a->depth_why = "mirrored by EXIF, and this mpv does not mirror it";
        fprintf(stderr, "xrdome: note: EXIF Orientation %d mirrors this photo and mpv %d.%d does "
                        "not; showing it in 2D (mpv 0.42 does)\n",
                orient, a->mpv_major, a->mpv_minor);
    } else if (!xrd_renderer_set_depth(a->r, m.px, m.w, m.h, err, sizeof err)) {
        a->depth_why = "the GPU did not take it";
        fprintf(stderr, "xrdome: warning: depth %s: %s; showing the photo in 2D\n", path, err);
    } else {
        a->depth_w = m.w;
        a->depth_h = m.h;
        a->depth_checked = false;
        a->depth_why = NULL;
        fprintf(stderr, "depth: %dx%d from %s\n", m.w, m.h, from);
    }
    xrd_depthmap_free(&m);
    return true;
}

/* Asks the depth helper (spec 7) for path; false when there is none to ask.
 * prefetch: for a photo that is not open (the next and previous). */
static bool depth_ask(xrd_app *a, const char *path, bool prefetch, char *abs, size_t abslen)
{
    if (!a->dc || xrd_depthclient_failed(a->dc) || xrd_depth_abs_path(path, abs, abslen) != 0)
        return false;
    return xrd_depthclient_request(a->dc, abs, prefetch);
}

/* The photos either side of the open one, so that their depth is ready when
 * PageDown or PageUp gets there; the next one is asked last, so it is the
 * helper's newest prefetch. */
static void depth_prefetch(xrd_app *a)
{
    if (a->photos.count < 2)
        return;
    for (int d = -1; d <= 1; d += 2) {
        long i = xrd_imagedir_step(&a->photos, a->photo_index, d);
        char abs[XRD_DEPTH_PATH_MAX];
        if (i >= 0)
            depth_ask(a, a->photos.path[i], true, abs, sizeof abs);
    }
}

/* Depth 3D (spec 6 and 7 of the depth 3D design): the depth of the photo now
 * open, from the cache, else asked of the depth helper (the reply arrives in
 * depth_poll), or why there is none. At start and for each photo; one depth:
 * line, warning or note per photo. The depth's aspect is checked against the
 * picture once it is shown (depth_fits). */
static void depth_load(xrd_app *a)
{
    xrd_renderer_set_depth(a->r, NULL, 0, 0, NULL, 0);
    a->depth_w = a->depth_h = 0;
    a->depth_checked = false;
    a->depth_why = "not a photo";
    a->depth_asked[0] = '\0';
    if (!a->det.image) {
        if (!a->cfg->depth_helper[0])
            a->depth_why = "a film's depth comes from the depth helper, which is off";
        else
            a->depth_why = a->video_failed ? "the depth helper failed" : NULL;
        return;
    }
    const char *file = a->cfg->file;
    char key[XRD_DEPTH_KEY_LEN + 1], dir[4096], path[4200];
    if (xrd_depth_key(file, key) != 0) {
        a->depth_why = "the photo cannot be read";
        fprintf(stderr, "xrdome: warning: cannot read %s for its depth key: %s\n", file,
                strerror(errno));
        return;
    }
    if (xrd_depth_cache_dir(dir, sizeof dir) != 0 ||
        xrd_depth_cache_path(dir, key, path, sizeof path) != 0) {
        a->depth_why = "no cache directory (XDG_CACHE_HOME and HOME unset)";
        fprintf(stderr, "depth: none: %s\n", a->depth_why);
        return;
    }
    if (depth_take(a, path, "the cache"))
        return;
    if (depth_ask(a, file, false, a->depth_asked, sizeof a->depth_asked)) {
        a->depth_why = "the depth helper is making it";
        fprintf(stderr, "depth: none cached; asking the depth helper\n");
        return;
    }
    a->depth_asked[0] = '\0';
    if (a->dc) {
        a->depth_why = "none cached, and the depth helper is not running";
        fprintf(stderr, "depth: none cached; 2D (the depth helper is not running)\n");
    } else {
        a->depth_why = "none cached (xrdome-tools depth photo makes it)";
        fprintf(stderr, "depth: none cached; 2D (xrdome-tools depth photo makes it)\n");
    }
}

/* Video depth (spec 8 of the depth 3D design, first half). The frame sink
 * stops: one line with what went out and came back. */
static void video_stop(xrd_app *a)
{
    if (!a->sink)
        return;
    xrd_framesink_stats st;
    xrd_framesink_get_stats(a->sink, &st);
    uint64_t back = xrd_vring_depth_head(xrd_framesink_ring(a->sink));
    fprintf(stderr,
            "depth: video stopped: %llu frames out, %llu skipped (readbacks busy), %llu depth "
            "maps back; CPU %.3f ms a frame on average, %.3f ms max; %llu depth maps drawn, "
            "%.1f frames old on average, %llu at most\n",
            (unsigned long long)st.sent, (unsigned long long)st.skipped,
            (unsigned long long)back,
            a->sink_steps ? (double)a->sink_ns / (double)a->sink_steps / 1e6 : 0.0,
            (double)a->sink_max_ns / 1e6, (unsigned long long)a->vdepth_taken,
            a->vdepth_taken ? (double)a->vdepth_age_sum / (double)a->vdepth_taken : 0.0,
            (unsigned long long)a->vdepth_age_max);
    xrd_framesink_free(a->sink);
    a->sink = NULL;
    /* the film's depth belongs to the ring that made it: the next ring (3D
     * on again) starts without, and the draw is 2D until its first map */
    if (a->depth_w > 0 && a->vdepth_seen > 0) {
        xrd_renderer_set_depth(a->r, NULL, 0, 0, NULL, 0);
        a->depth_w = a->depth_h = 0;
    }
}

/* The helper cannot take the film's frames: no more for this run. */
static void video_failed(xrd_app *a)
{
    a->video_failed = true;
    a->depth_why = "the depth helper failed";
    video_stop(a);
}

/* A ring and the request that the helper map it; the helper is started by
 * the first request when it is not running. */
static void video_start(xrd_app *a)
{
    if (!a->dc) {
        char sock[256];
        if (xrd_depth_socket_path(sock, sizeof sock) != 0) {
            fprintf(stderr, "xrdome: note: depth helper: no socket path (XDG_RUNTIME_DIR or "
                            "TMPDIR is too long); the film stays 2D\n");
            a->video_failed = true;
            return;
        }
        if (!(a->dc = xrd_depthclient_new(sock, a->cfg->depth_helper))) {
            fprintf(stderr, "xrdome: note: depth helper: out of memory; the film stays 2D\n");
            a->video_failed = true;
            return;
        }
    }
    char name[32], err[512] = "";
    if (xrd_vring_shm_name(name, sizeof name, (long)getpid(), ++a->sink_n) != 0 ||
        !(a->sink = xrd_framesink_new(name, err, sizeof err))) {
        fprintf(stderr, "xrdome: warning: depth: %s; the film stays 2D\n",
                err[0] ? err : "no shared-memory name");
        video_failed(a);
        return;
    }
    if (!xrd_depthclient_video(a->dc, name)) {
        fprintf(stderr, "xrdome: note: depth helper: %s; the film stays 2D\n",
                xrd_depthclient_error(a->dc));
        video_failed(a);
        return;
    }
    a->sink_ns = a->sink_max_ns = a->sink_steps = 0;
    a->vdepth_seen = a->vdepth_taken = a->vdepth_age_sum = a->vdepth_age_max = 0;
    fprintf(stderr, "depth: video frames to the depth helper (%dx%d through %s)\n", XRD_VRING_W,
            XRD_VRING_H, name);
}

/* The helper's newest depth map for the film, when there is one newer than
 * the last: into the depth texture (spec 8), whatever the frame it was made
 * from (1 or 2 frames old, counted for the stop line). Never waits: a map
 * the helper is writing at that moment is taken next frame. */
static void video_depth(xrd_app *a)
{
    void *ring = xrd_framesink_ring(a->sink);
    uint64_t frame;
    if (!a->vdepth) {
        a->vdepth = malloc(XRD_VRING_DEPTH_BYTES);
        if (!a->vdepth)
            return;
    }
    if (!xrd_vring_depth_read(ring, &a->vdepth_seen, a->vdepth, &frame))
        return;
    char err[128] = "";
    if (!xrd_renderer_set_depth(a->r, a->vdepth, XRD_VRING_W, XRD_VRING_H, err, sizeof err)) {
        fprintf(stderr, "xrdome: warning: depth: %s; the film stays 2D\n", err);
        video_failed(a);
        return;
    }
    /* The frame sent is scaled over the whole picture whatever its aspect,
     * so the depth lines up in picture coordinates: no aspect check. */
    a->depth_w = XRD_VRING_W;
    a->depth_h = XRD_VRING_H;
    a->depth_checked = true;
    a->depth_why = NULL;
    uint64_t head = xrd_vring_frame_head(ring);
    uint64_t age = head > frame ? head - frame : 0;
    a->vdepth_taken++;
    a->vdepth_age_sum += age;
    if (age > a->vdepth_age_max)
        a->vdepth_age_max = age;
}

/* Once per frame: frames go out only for a flat mono film with 3D on
 * (xrd_framesink_wanted); otherwise nothing is scaled or read back, and a
 * sink still running is stopped. Its CPU time (the depth upload included)
 * goes in the timing log. */
static void video_step(xrd_app *a, xrd_timing *timing, unsigned tex)
{
    const xrd_controls *c = a->ctl;
    if (a->video_failed || !xrd_framesink_wanted(a->det.image, c->proj, c->layout, c->depth_on,
                                                 a->cfg->depth_helper[0] != '\0')) {
        video_stop(a);
        return;
    }
    uint64_t t0 = xrd_now_ns();
    if (!a->sink)
        video_start(a);
    if (!a->sink)
        return;
    xrd_framesink_step(a->sink, tex, xrd_player_frame_serial(a->p), t0);
    video_depth(a);
    if (!a->sink) /* the upload failed: video_failed stopped it */
        return;
    uint64_t dt = xrd_now_ns() - t0;
    xrd_timing_frames_out(timing, dt);
    a->sink_ns += dt;
    a->sink_steps++;
    if (dt > a->sink_max_ns)
        a->sink_max_ns = dt;
}

/* Once per frame, without waiting: the depth helper's replies. The one for
 * the open photo switches it to 3D; the others (prefetches, photos left
 * behind) only warmed the cache. A helper that cannot be started or goes away
 * gives one note for the run. */
static void depth_poll(xrd_app *a)
{
    if (!a->dc)
        return;
    xrd_depth_reply rep;
    xrd_dc_event ev;
    while ((ev = xrd_depthclient_poll(a->dc, xrd_now_ns(), &rep)) != XRD_DC_NONE) {
        if (ev == XRD_DC_FAILED && !a->det.image) {
            fprintf(stderr, "xrdome: note: depth helper: %s; the film stays 2D\n",
                    xrd_depthclient_error(a->dc));
            video_failed(a);
            continue;
        }
        if (ev == XRD_DC_REPLY && rep.video) {
            if (!a->sink || strcmp(rep.shm, xrd_framesink_shm_name(a->sink)) != 0)
                continue; /* a ring already stopped */
            if (rep.ok) {
                fprintf(stderr, "depth: the depth helper reads %s\n", rep.shm);
            } else {
                fprintf(stderr, "xrdome: note: depth helper: %s; the film stays 2D\n",
                        rep.error);
                video_failed(a);
            }
            continue;
        }
        if (ev == XRD_DC_FAILED) {
            fprintf(stderr, "xrdome: note: depth helper: %s; photos without cached depth stay "
                            "2D\n", xrd_depthclient_error(a->dc));
            if (a->depth_asked[0]) {
                a->depth_asked[0] = '\0';
                a->depth_why = "none cached, and the depth helper failed";
            }
            continue;
        }
        if (!a->depth_asked[0] || strcmp(rep.path, a->depth_asked) != 0)
            continue;
        a->depth_asked[0] = '\0';
        if (!rep.ok) {
            a->depth_why = "the depth helper could not make it";
            fprintf(stderr, "xrdome: note: depth helper: %s; showing the photo in 2D\n",
                    rep.error);
        } else if (!depth_take(a, rep.depth, "the depth helper")) {
            a->depth_why = "the depth helper's file is missing";
            fprintf(stderr, "xrdome: warning: depth %s: the depth helper made it, but it is "
                            "not there; showing the photo in 2D\n", rep.depth);
        }
    }
}

/* Once the open photo is shown: its depth must have the picture's aspect, or
 * it was made for another orientation (EXIF that mpv does not apply, outside
 * JPEG); then it is dropped with a warning. */
static void depth_fits(xrd_app *a)
{
    if (a->depth_checked || a->depth_w == 0)
        return;
    a->depth_checked = true;
    int vw = 0, vh = 0;
    xrd_player_video_size(a->p, &vw, &vh);
    if (xrd_depth_aspect_matches(a->depth_w, a->depth_h, vw, vh))
        return;
    fprintf(stderr, "xrdome: warning: the depth (%dx%d) does not fit the picture shown (%dx%d): "
                    "made for another orientation; showing the photo in 2D\n",
            a->depth_w, a->depth_h, vw, vh);
    xrd_renderer_set_depth(a->r, NULL, 0, 0, NULL, 0);
    a->depth_w = a->depth_h = 0;
    a->depth_why = "it does not fit the picture shown";
}

/* P (spec 5.7): the other projection, with its own zoom. A layout that was
 * only a frame-shape or fallback guess, and not chosen with M or S, is worked
 * out again for the new projection, and packing likewise when it was auto. */
static void key_proj(xrd_app *a)
{
    xrd_controls *c = a->ctl;
    xrd_layout layout0 = c->layout;
    xrd_packing packing0 = c->packing;
    xrd_controls_toggle_proj(c);
    a->proj_keyed = true;
    if (!c->layout_chosen &&
        (a->det.layout_src == XRD_SRC_SHAPE || a->det.layout_src == XRD_SRC_FALLBACK ||
         a->det.layout_src == XRD_SRC_IMAGE)) {
        xrd_detect_relayout(frame_aspect_now(a), c->proj, &c->layout, &c->packing);
        if (a->det.packing_src != XRD_SRC_AUTO)
            c->packing = packing0;
        /* Still a guess, not a choice: it is not saved for the file. */
        a->mem->start.layout = c->layout;
        a->mem->start.packing = c->packing;
    }
    fprintf(stderr, "proj: %s\n", xrd_value_proj_name(c->proj));
    if (c->layout != layout0 || c->packing != packing0)
        print_layout(a);
    apply_format(a);
}

/* Spec 6.4 and flat-projection 5.2 to 5.7 keys. Returns true when the key
 * asks to quit. Key repeat is ignored except for zoom and seek. */
static bool handle_key_action(const SDL_KeyboardEvent *k, xrd_app *a)
{
    xrd_controls *ctl = a->ctl;
    bool shift = (k->keysym.mod & KMOD_SHIFT) != 0;
    if (k->repeat && !xrd_key_repeats(k->keysym.sym))
        return false;
    switch (k->keysym.sym) {
    case SDLK_q:
        return true;
    case SDLK_SPACE:
        if (!k->repeat && !a->det.image) /* a photo has no time to pause or seek in */
            xrd_player_toggle_pause(a->p);
        break;
    case SDLK_LEFT:
        if (!a->det.image)
            xrd_player_seek(a->p, -XRD_SEEK_SMALL_S);
        break;
    case SDLK_RIGHT:
        if (!a->det.image)
            xrd_player_seek(a->p, XRD_SEEK_SMALL_S);
        break;
    case SDLK_UP:
        if (!a->det.image)
            xrd_player_seek(a->p, XRD_SEEK_LARGE_S);
        break;
    case SDLK_DOWN:
        if (!a->det.image)
            xrd_player_seek(a->p, -XRD_SEEK_LARGE_S);
        break;
    case SDLK_PAGEDOWN: /* acted on in the main loop, which owns the player */
    case SDLK_PAGEUP:
        if (a->photos.count > 0)
            a->nav = k->keysym.sym == SDLK_PAGEDOWN ? 1 : -1;
        break;
    case SDLK_r:
        xrd_tracker_recenter(a->trk, !shift); /* R: yaw only, Shift+R: full */
        xrd_controls_recenter(ctl);
        break;
    case SDLK_LEFTBRACKET:
        xrd_controls_zoom_out(ctl);
        break;
    case SDLK_RIGHTBRACKET:
        xrd_controls_zoom_in(ctl);
        break;
    case SDLK_f:
        if (k->repeat)
            break;
        if (shift) /* Shift+F: sharpening strength; F: filter */
            xrd_controls_step_sharpen(ctl);
        else
            xrd_controls_cycle_filter(ctl);
        break;
    case SDLK_s:
        if (!k->repeat)
            xrd_controls_toggle_swap(ctl);
        break;
    case SDLK_m:
        if (!k->repeat) {
            xrd_packing packing0 = ctl->packing;
            xrd_controls_cycle_layout(ctl, frame_aspect_now(a));
            a->layout_keyed = true;
            if (ctl->packing != packing0)
                a->packing_keyed = true;
            print_layout(a);
            apply_format(a);
        }
        break;
    case SDLK_p:
        if (!k->repeat)
            key_proj(a);
        break;
    case SDLK_z:
        if (k->repeat)
            break;
        if (xrd_controls_toggle_zoom_mode(ctl))
            fprintf(stderr, "zoom mode: %s\n", xrd_value_zoom_mode_name(ctl->zoom_mode));
        else
            fprintf(stderr, "zoom mode: magnify (VR180 has no screen mode)\n");
        break;
    case SDLK_l:
        if (k->repeat)
            break;
        if (xrd_controls_toggle_lock(ctl)) {
            if (!ctl->screen_locked) { /* the screen reappears straight ahead */
                xrd_tracker_recenter(a->trk, false);
                xrd_controls_recenter(ctl);
            }
            fprintf(stderr, "screen: %s\n", ctl->screen_locked ? "head-locked" : "fixed");
        } else {
            fprintf(stderr, "screen: fixed (VR180 cannot be head-locked)\n");
        }
        break;
    case SDLK_c:
        if (k->repeat)
            break;
        if (xrd_controls_toggle_curve(ctl))
            print_curve(ctl, "");
        else
            fprintf(stderr, "screen curve: none (VR180 has no flat screen)\n");
        break;
    case SDLK_SLASH: /* '?' is Shift+/ on a US layout */
        if (shift && !k->repeat)
            print_keys();
        break;
    case SDLK_QUESTION:
        if (!k->repeat)
            print_keys();
        break;
    case SDLK_b:
        if (!k->repeat) {
            bool now = !xrd_player_deband(a->p);
            if (!xrd_player_set_deband(a->p, now))
                fprintf(stderr, "deband: mpv would not change it\n");
            else
                fprintf(stderr, "deband: %s\n", xrd_player_deband(a->p) ? "on" : "off");
        }
        break;
    case SDLK_i:
        if (!k->repeat)
            print_info(a);
        break;
    case SDLK_d: /* depth 3D: photos and flat mono films (spec 7 and 8 of the depth 3D design) */
    case SDLK_MINUS:
    case SDLK_EQUALS:
    case SDLK_COMMA:
    case SDLK_PERIOD:
        if (!a->det.image && (ctl->proj != XRD_PROJ_FLAT || ctl->layout != XRD_LAYOUT_MONO)) {
            if (!k->repeat)
                fprintf(stderr, "depth: photos and flat mono films only\n");
            break;
        }
        if (k->keysym.sym == SDLK_d && !k->repeat)
            xrd_controls_toggle_depth(ctl);
        else if (k->keysym.sym == SDLK_MINUS)
            xrd_controls_depth_weaker(ctl);
        else if (k->keysym.sym == SDLK_EQUALS)
            xrd_controls_depth_stronger(ctl);
        else if (k->keysym.sym == SDLK_COMMA)
            xrd_controls_plane_further(ctl);
        else if (k->keysym.sym == SDLK_PERIOD)
            xrd_controls_plane_nearer(ctl);
        if (a->depth_why && !k->repeat)
            fprintf(stderr, "depth: none for this %s: %s\n", a->det.image ? "photo" : "film",
                    a->depth_why);
        break;
    default:
        break;
    }
    return false;
}

/* handle_key_action, then a status line for each display value it changed. */
static bool handle_key(const SDL_KeyboardEvent *k, xrd_app *a)
{
    const xrd_controls before = *a->ctl;
    bool quit = handle_key_action(k, a);
    report_display_changes(&before, a->ctl);
    return quit;
}

/* Handles --forget and --no-memory and loads the file's record (applied
 * later by xrd_session_start). Silent when the file has no key or there is
 * no state directory. */
static void memory_load(xrd_memory *m, const xrd_config *cfg)
{
    memset(m, 0, sizeof *m);
    if (xrd_filestate_key(cfg->file, m->key, sizeof m->key) != 0 ||
        xrd_filestate_dir(m->dir, sizeof m->dir) != 0)
        return;
    if (cfg->forget) {
        int d = xrd_filestate_delete(m->dir, m->key);
        int de = errno;
        if (d == 1) {
            fprintf(stderr, "settings: forgot %s\n", m->key);
        } else if (d < 0) {
            char rp[4200];
            if (xrd_filestate_record_path(m->dir, m->key, rp, sizeof rp) != 0)
                snprintf(rp, sizeof rp, "%s", m->key);
            fprintf(stderr, "xrdome: warning: cannot forget settings record %s: %s\n", rp,
                    strerror(de));
            m->block_save = true; /* the stale record is neither loaded nor overwritten */
            return;
        }
    }
    if (cfg->no_memory)
        return;
    m->on = true;

    char path[4200], err[256] = "";
    if (xrd_filestate_record_path(m->dir, m->key, path, sizeof path) != 0)
        return;
    switch (xrd_filestate_load(path, &m->loaded, err, sizeof err)) {
    case XRD_FS_OK:
        m->have_loaded = true;
        break;
    case XRD_FS_UNSUPPORTED:
        m->block_save = true;
        fprintf(stderr,
                "xrdome: note: settings record %s is from a newer xrdome; ignoring it "
                "(--forget removes it)\n", path);
        break;
    case XRD_FS_BAD:
        m->block_save = true;
        fprintf(stderr, "xrdome: warning: settings record %s: %s; ignoring it "
                        "(--forget removes it)\n", path, err);
        break;
    case XRD_FS_NONE:
        break;
    }
}

/* Writes the record at quit when something is set and it differs from what
 * was loaded. Every record states the projection in effect at quit (spec
 * 5.9), so proj is always set in what is written and in the saved line.
 * Failure only warns; the exit status is not touched. */
static void memory_save(const xrd_memory *m, const xrd_config *cfg, const char *file,
                        const xrd_controls *ctl, double position, double duration)
{
    xrd_file_settings end;
    xrd_session_settings(ctl, m->start.content_hfov, m->start.content_vfov, &end);
    xrd_filestate out; /* distinct from m->loaded: collect clears it first */
    xrd_filestate_collect(m->have_loaded ? &m->loaded : NULL, m->explicit_flags, &m->start,
                          &end, &out);
    /* The position (--resume) is not a view setting: collect leaves it out.
     * Written only while resume is on and the position at quit is inside the
     * resumable part of the file; a position outside it removes the old one.
     * With resume off, a record's position is kept as it was. */
    double pos = 0.0;
    int rs = xrd_session_resume_save(cfg, position, duration, &pos);
    if (rs > 0 || (rs < 0 && m->have_loaded && (m->loaded.set & XRD_SET_POSITION))) {
        out.set |= XRD_SET_POSITION;
        out.v.position = rs > 0 ? pos : m->loaded.v.position;
    }
    if (out.set == 0 && !m->have_loaded)
        return;
    out.set |= XRD_SET_PROJ;
    out.v.proj = end.proj;
    if (m->have_loaded && xrd_filestate_equal(&m->loaded, &out))
        return;

    char err[256] = "", text[256];
    if (xrd_filestate_save(m->dir, m->key, &out, file, err, sizeof err) != 0)
        fprintf(stderr, "xrdome: warning: cannot save settings: %s\n", err);
    else if (xrd_filestate_describe(&out, ~0u, text, sizeof text) > 0)
        fprintf(stderr, "settings: saved %s\n", text);
}

/* Probe, detection and the starting state, before the window opens (plan
 * Task 8): the notes, the detect: line and the settings: restored line. */
static void detect_and_start(xrd_app *a, const xrd_config *cfg, xrd_memory *mem,
                             xrd_session *ss)
{
    char perr[256] = "";
    xrd_probe_status pst = xrd_probe_run(cfg->ffprobe, cfg->file, XRD_PROBE_TIMEOUT_MS,
                                         &a->probe, perr, sizeof perr);
    a->have_probe = pst == XRD_PROBE_OK;

    const xrd_filestate *rec = mem->have_loaded ? &mem->loaded : NULL;
    xrd_detect_given flags, record;
    xrd_session_given(cfg, rec, &flags, &record);
    xrd_detect_resolve(&flags, rec ? &record : NULL, a->have_probe ? &a->probe : NULL,
                       cfg->file, &a->det);

    /* At most one note for a failed probe: a failure leaves no probe data, so
     * the only other possible note is the fallback, and the two are one line. */
    int first = 0;
    if (pst == XRD_PROBE_FAILED) {
        if (a->det.note_count > 0) {
            fprintf(stderr, "xrdome: note: %s; %s\n", perr, a->det.notes[0]);
            first = 1;
        } else {
            fprintf(stderr, "xrdome: note: %s\n", perr);
        }
    }
    for (int i = first; i < a->det.note_count; i++)
        fprintf(stderr, "xrdome: note: %s\n", a->det.notes[i]);
    char line[256];
    if (xrd_detect_describe(&a->det, line, sizeof line) > 0)
        fprintf(stderr, "detect: %s\n", line);

    xrd_session_start(cfg, rec, &a->det, ss);
    /* --resume: the record's position, handed to mpv as its start option (main). */
    double rpos = 0.0;
    unsigned shown = ss->applied;
    if (!a->det.image && xrd_session_resume_start(cfg, rec, &rpos)) {
        mem->resume = true;
        mem->resume_at = rpos;
        shown |= XRD_SET_POSITION;
    }
    char text[256];
    if (rec && shown && xrd_filestate_describe(rec, shown, text, sizeof text) > 0)
        fprintf(stderr, "settings: restored %s\n", text);
    if (a->det.proj == XRD_PROJ_FLAT && (cfg->explicit_flags & XRD_SET_CONTENT_FOV))
        fprintf(stderr, "xrdome: note: --content-fov applies to VR180 only; "
                        "ignored for this flat video\n");

    mem->start = ss->s;
    mem->explicit_flags = ss->explicit_flags;
    a->content_hfov = ss->s.content_hfov;
    a->content_vfov = ss->s.content_vfov;
}

/* PageDown / PageUp (a.nav) in a directory of photos: the settings of the
 * photo that was open are saved, the next one is detected and its record
 * applied like at start, and the player loads it. What is not remembered per
 * file (filter, sharpening, curve, screen lock, mouse look) carries over. The
 * old picture stays up until the new one has been drawn. *rendered is false
 * until that happens, so that a photo never shown never saves anything. */
static int switch_photo(xrd_app *a, xrd_config *cfg, xrd_session *ss, bool *rendered, char *err,
                        size_t errlen)
{
    long next = xrd_imagedir_step(&a->photos, a->photo_index, a->nav);
    bool forward = a->nav > 0;
    a->nav = 0;
    if (next < 0) {
        fprintf(stderr, "photo: %zu of %zu (%s of the directory)\n", a->photo_index + 1,
                a->photos.count, forward ? "end" : "start");
        return 0;
    }
    xrd_memory *mem = a->mem;
    if (mem->on && !mem->block_save && *rendered)
        memory_save(mem, cfg, cfg->file, a->ctl, -1.0, -1.0);
    a->photo_index = (size_t)next;
    snprintf(cfg->file, sizeof cfg->file, "%s", a->photos.path[next]);
    const char *name = strrchr(cfg->file, '/');
    fprintf(stderr, "photo: %zu of %zu: %s\n", a->photo_index + 1, a->photos.count,
            name ? name + 1 : cfg->file);

    xrd_config load = *cfg; /* --forget was for the photo the run opened with */
    load.forget = false;
    memory_load(mem, &load);
    detect_and_start(a, cfg, mem, ss);

    const xrd_controls old = *a->ctl;
    xrd_session_controls(ss, cfg, a->ctl);
    a->ctl->filter = old.filter;
    a->ctl->sharpen = old.sharpen;
    a->ctl->screen_curved = old.screen_curved;
    a->ctl->screen_curve = old.screen_curve;
    a->ctl->screen_locked = old.screen_locked;
    a->ctl->mouse_yaw_deg = old.mouse_yaw_deg;
    a->ctl->mouse_pitch_deg = old.mouse_pitch_deg;
    a->ctl->depth_on = old.depth_on;
    a->proj_keyed = a->layout_keyed = a->packing_keyed = false;
    xrd_renderer_set_content_fov(a->r, ss->s.content_hfov, ss->s.content_vfov);
    depth_load(a);
    depth_prefetch(a);
    apply_format(a);
    *rendered = false;
    return xrd_player_load(a->p, cfg->file, err, errlen);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--version")) { /* before the config asks for a file */
        printf("xrdome %s\n", XRD_VERSION);
        return 0;
    }
    char err[512] = "";
    char ini[4096];
    xrd_config cfg;
    bool have_ini = xrd_config_default_path(ini, sizeof ini) == 0;
    if (xrd_config_load(&cfg, have_ini ? ini : NULL, argc, argv, err, sizeof err) != 0)
        return fail("%s", err);

    xrd_app app;
    memset(&app, 0, sizeof app);
    /* A directory argument opens its first photo; PageDown / PageUp walk it. */
    if (xrd_imagedir_is_dir(cfg.file)) {
        if (xrd_imagedir_open(cfg.file, &app.photos, err, sizeof err) != 0)
            return fail("%s", err);
        snprintf(cfg.file, sizeof cfg.file, "%s", app.photos.path[0]);
    }
    xrd_memory mem;
    memory_load(&mem, &cfg);
    app.cfg = &cfg;
    app.mem = &mem;
    xrd_session ss;
    detect_and_start(&app, &cfg, &mem, &ss);

    xrd_pose_source *pose = xrd_pose_source_create(cfg.pose, err, sizeof err);
    if (!pose) {
        xrd_imagedir_free(&app.photos);
        return fail("%s", err);
    }

    int rc = 1;
    xrd_tracker *trk = NULL;
    xrd_timing *timing = NULL;
    SDL_Window *win = NULL;
    SDL_GLContext gl = NULL;
    xrd_renderer *r = NULL;
    /* --window-eye left|right: both eyes are drawn into this offscreen target,
     * twice the window's width, and one half is copied to the window. */
    GLuint eye_fbo = 0, eye_tex = 0;
    xrd_player *p = NULL;
    int presented = 0; /* frames swapped since the first video frame (--frames) */
    bool done = false; /* --frames reached, or the glasses went away */
    bool rendered = false; /* a video frame was drawn (gates saving the settings) */
    bool sbs_switched = false; /* xrdome switched the glasses to SBS: switch back at exit */
    bool glasses_gone = false; /* the glasses' output disconnected during playback */
    xrd_controls ctl;      /* valid once rendered is set */
    int press_keys[XRD_PRESS_MAX]; /* --press: SDL key codes, pressed in order */
    int press_n = 0, press_next = 0;

    trk = xrd_tracker_new(pose);
    if (!trk) {
        fail("%s", "out of memory");
        goto out;
    }
    timing = xrd_timing_open(cfg.timing_log[0] ? cfg.timing_log : NULL);
    if (!timing) {
        fprintf(stderr, "xrdome: cannot create timing log %s: %s\n", cfg.timing_log,
                strerror(errno));
        goto out;
    }

    const xrd_platform plat = xrd_platform_current();
    const bool use_x11 = xrd_platform_uses_x11(plat);
    if (use_x11) {
        SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
        /* xrdome needs SDL's X11 driver (Xorg, or XWayland under a Wayland
         * compositor). SDL builds with a Wayland backend, such as sdl2-compat on
         * Arch and SteamOS, prefer it when WAYLAND_DISPLAY is set; an explicit
         * SDL_VIDEODRIVER still wins. */
        if (!getenv("SDL_VIDEODRIVER"))
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11");
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fail("SDL_Init: %s", SDL_GetError());
        goto out;
    }

    /* --press: SDL key names to key codes; "comma" stands for ",", which
     * separates the names. */
    for (const char *k = cfg.press; k[0];) {
        size_t n = strcspn(k, ",");
        char name[32];
        if (n == 0 || n >= sizeof name || press_n == XRD_PRESS_MAX) {
            fail("--press: bad key list '%s'", cfg.press);
            goto out;
        }
        memcpy(name, k, n);
        name[n] = '\0';
        if ((press_keys[press_n++] = strcmp(name, "comma") == 0 ? SDLK_COMMA
                                                                : SDL_GetKeyFromName(name)) ==
            SDLK_UNKNOWN) {
            fail("--press: unknown key '%s'", name);
            goto out;
        }
        k += n + (k[n] == ',');
    }

    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;
    int w = WINDOWED_W, h = WINDOWED_H;
    if (cfg.window_w > 0) {
        w = cfg.window_w;
        h = cfg.window_h;
    } else if (cfg.window_eye != XRD_WINDOW_EYE_BOTH) {
        w = WINDOWED_W / 2; /* one eye of the default window */
    }
    int display = -1; /* SDL display index of the glasses; -1 when windowed */
    int refresh_hz = 0; /* 0 = unknown: pace at 60 Hz */
    Uint32 wflags = SDL_WINDOW_OPENGL;
    if (!cfg.windowed) {
        xrd_output_info outs[MAX_OUTPUTS];
        int n = xrd_output_enumerate(outs, MAX_OUTPUTS);
        if (n < 0) {
            fail("cannot list outputs: %s", SDL_GetError());
            goto out;
        }
        int idx = -1;
        int sel = xrd_output_select(outs, n, cfg.output, &idx);
        xrd_sbs_result sbs = {.outcome = XRD_SBS_NOT_ASKED};
        bool sbs_tried = false;
        if (sel != XRD_OUT_OK && cfg.sbs_switch && xrd_platform_has_xrdriver(plat) &&
            xrd_output_sbs_candidate(outs, n, cfg.output, sel)) {
            sbs_tried = true;
            sel = xrd_sbs_switch(xrd_sbs_sdl_ops(), NULL, cfg.output, XRD_SBS_WAIT_MS, outs,
                                 MAX_OUTPUTS, &n, &idx, &sbs);
            if (sbs.outcome == XRD_SBS_SWITCHED) {
                sbs_switched = true;
                fprintf(stderr, "display: switched the glasses to SBS in %.1f s\n",
                        (double)sbs.waited_ms / 1000.0);
            }
        }
        if (sel != XRD_OUT_OK) {
            fail("%s", xrd_output_strerror(sel, cfg.output));
            if (sbs_tried)
                fail("automatic SBS switch: %s", sbs.why);
            if (sel == XRD_OUT_NOT_FOUND) {
                char list[1024];
                xrd_output_list(outs, n, list, sizeof list);
                fail("outputs: %s", list);
            }
            goto out;
        }
        display = idx; /* xrd_output_enumerate fills outs[] by SDL display index */
        refresh_hz = outs[idx].refresh_hz;
        x = outs[idx].x;
        y = outs[idx].y;
        w = outs[idx].w;
        h = outs[idx].h;
        wflags |= SDL_WINDOW_BORDERLESS;
        /* Under a Wayland compositor (XWayland) a borderless window at the
         * output's position may be ignored or tiled; fullscreen on the
         * display the window is created on is placed and can be scanned out
         * directly. Xorg keeps the borderless window. */
        if (xrd_platform_force_fullscreen(plat) ||
            xrd_output_use_fullscreen(getenv("WAYLAND_DISPLAY"), getenv("XDG_SESSION_TYPE")))
            wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (xrd_platform_gl_forward_compat(plat)) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        /* Pixels, not points: the drawable is the glasses' full 3840x1080
         * even if the display is scaled (SDL_GL_GetDrawableSize below). */
        wflags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }

    win = SDL_CreateWindow("xrdome", x, y, w, h, wflags);
    if (!win) {
        fail("SDL_CreateWindow: %s", SDL_GetError());
        goto out;
    }
    gl = SDL_GL_CreateContext(win);
    if (!gl) {
        fail(use_x11 ? "cannot create a GL 3.3 core context on EGL: %s"
                     : "cannot create a GL 3.3 core context: %s",
             SDL_GetError());
        goto out;
    }
    if (SDL_GL_SetSwapInterval(1) != 0)
        fprintf(stderr, "xrdome: warning: cannot enable vsync: %s\n", SDL_GetError());

    void *x11_display = NULL; /* for mpv; NULL where there is no X11 (macOS) */
    if (use_x11) {
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        if (!SDL_GetWindowWMInfo(win, &wm) || wm.subsystem != SDL_SYSWM_X11) {
            fail("%s", "SDL did not use its X11 driver (Xorg or XWayland); "
                      "unset SDL_VIDEODRIVER or set it to x11");
            goto out;
        }
#if defined(SDL_VIDEO_DRIVER_X11)
        x11_display = wm.info.x11.display;
#else
        fail("%s", "this SDL was built without its X11 driver");
        goto out;
#endif
    }

    /* The refresh rate of the display the window is on (the glasses, or
     * wherever --windowed landed); on failure keep the enumerated value or
     * the 60 Hz fallback. */
    {
        int di = SDL_GetWindowDisplayIndex(win);
        SDL_DisplayMode dm;
        if (di >= 0 && SDL_GetCurrentDisplayMode(di, &dm) == 0 && dm.refresh_rate > 0)
            refresh_hz = dm.refresh_rate;
    }

    /* win_w x win_h is the window's drawable; out_w x out_h is what the eye
     * pass draws into: the same, or with --window-eye twice as wide. */
    int out_w, out_h, win_w, win_h;
    SDL_GL_GetDrawableSize(win, &win_w, &win_h);
    out_w = win_w;
    out_h = win_h;
    if (cfg.window_eye != XRD_WINDOW_EYE_BOTH) {
        GLint max_tex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
        out_w = win_w * 2;
        if (out_w > max_tex) {
            snprintf(err, sizeof err, "--window-eye: a %d pixel wide window needs a %d pixel "
                     "wide texture, and this GPU's largest is %d", win_w, out_w, (int)max_tex);
            fail("%s", err);
            goto out;
        }
        glGenTextures(1, &eye_tex);
        glBindTexture(GL_TEXTURE_2D, eye_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, out_w, out_h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        glGenFramebuffers(1, &eye_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, eye_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eye_tex, 0);
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (st != GL_FRAMEBUFFER_COMPLETE) {
            snprintf(err, sizeof err, "--window-eye: cannot make a %dx%d render target "
                     "(status 0x%x)", out_w, out_h, (unsigned)st);
            fail("%s", err);
            goto out;
        }
    }

    r = xrd_renderer_new(err, sizeof err);
    if (!r) {
        fail("renderer: %s", err);
        goto out;
    }
    /* One more than --mpv-opt allows: the resume position goes first, so any
     * --mpv-opt (and an explicit start, which suppresses it) comes after. */
    const char *mpv_opts[sizeof cfg.mpv_opts / sizeof cfg.mpv_opts[0] + 1];
    int n_opts = 0;
    char start_opt[64];
    if (mem.resume) {
        snprintf(start_opt, sizeof start_opt, "start=%.3f", mem.resume_at);
        mpv_opts[n_opts++] = start_opt;
    }
    for (int i = 0; i < cfg.mpv_opt_count; i++)
        mpv_opts[n_opts++] = cfg.mpv_opts[i];
    const char *hwdec = cfg.hwdec[0] ? cfg.hwdec : xrd_platform_default_hwdec(plat);
    xrd_player_options popt = {.profile = cfg.mpv_profile, .opts = mpv_opts,
                               .opt_count = n_opts, .log_level = cfg.mpv_log,
                               .fbo_format = cfg.fbo_format, .deband = cfg.deband,
                               .resume_at = mem.resume ? mem.resume_at : 0.0,
                               .scale = cfg.mpv_scale[0] ? cfg.mpv_scale : NULL,
                               .hwdec = hwdec, .image = app.det.image};
    p = xrd_player_open_ex(cfg.file, gl_get_proc, x11_display, &popt, err, sizeof err);
    if (!p) {
        fail("%s", err);
        goto out;
    }
    xrd_session_controls(&ss, &cfg, &ctl);
    /* A film starts in 2D: its depth needs the helper running beside it (D). */
    if (!app.det.image)
        ctl.depth_on = false;
    app.ctl = &ctl;
    app.trk = trk;
    app.p = p;
    app.timing = timing;
    app.r = r;
    xrd_mpv_version_parse(xrd_player_mpv_version(p), &app.mpv_major, &app.mpv_minor);
    /* Photos: the depth helper, started on the first request it is needed for. */
    if (app.det.image && cfg.depth_helper[0]) {
        char sock[256];
        if (xrd_depth_socket_path(sock, sizeof sock) != 0)
            fprintf(stderr, "xrdome: note: depth helper: no socket path (XDG_RUNTIME_DIR or "
                            "TMPDIR is too long); photos without cached depth stay 2D\n");
        else if (!(app.dc = xrd_depthclient_new(sock, cfg.depth_helper)))
            fprintf(stderr, "xrdome: note: depth helper: out of memory; photos without cached "
                            "depth stay 2D\n");
    }
    depth_load(&app);
    depth_prefetch(&app);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &app.max_tex);
    apply_format(&app); /* before the first xrd_player_update */
    xrd_renderer_set_content_fov(r, ss.s.content_hfov, ss.s.content_vfov);
    xrd_renderer_set_dither(r, cfg.dither);
    if (cfg.frames > 0)
        xrd_player_hold_at_end(p);
    int mip_draws = 0;      /* --mipmap-check: draws that wanted trilinear sampling */
    char mip_sizes[256] = ""; /* --mipmap-check: texture sizes drawn trilinearly, in order */
    int mip_w = 0, mip_h = 0;
    int shown_w = 0, shown_h = 0; /* texture size of the last video texture: line */

    const bool mouse_look = strcmp(pose->name, "null") == 0;
    bool size_dirty = true; /* recompute the per-eye aspect and tans on the next frame */
    /* If the swap does not wait for vsync (no swap interval, or the driver
     * ignores it), the loop is paced to this period after the swap. */
    const uint64_t period_ns = xrd_refresh_period_ns(refresh_hz);
    uint64_t prev_swap_ns = 0; /* after the previous swap and any pacing sleep */

    bool quit = false, hwdec_checked = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                quit = true;
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
                    quit = true;
                else if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    size_dirty = true;
                break;
            case SDL_KEYDOWN:
                if (handle_key(&ev.key, &app))
                    quit = true;
                break;
            case SDL_MOUSEMOTION:
                if (mouse_look && (ev.motion.state & SDL_BUTTON_LMASK))
                    xrd_controls_mouse_drag(&ctl, ev.motion.xrel, ev.motion.yrel);
                break;
            case SDL_DISPLAYEVENT:
                if (display < 0 || ev.display.event != SDL_DISPLAYEVENT_DISCONNECTED)
                    break;
                if ((int)ev.display.display == display) {
                    xrd_player_pause(p);
                    fprintf(stderr, "xrdome: warning: glasses disconnected, exiting\n");
                    quit = done = glasses_gone = true;
                } else if ((int)ev.display.display < display) {
                    display--; /* SDL renumbers the displays after the removed one */
                }
                break;
            default:
                break;
            }
        }
        if (quit)
            break;
        if (app.nav && switch_photo(&app, &cfg, &ss, &rendered, err, sizeof err) < 0) {
            fail("%s", err);
            goto out;
        }
        depth_poll(&app); /* never waits for the helper */

        if (xrd_player_update(p) < 0) {
            fail("%s", xrd_player_error(p));
            goto out;
        }
        if (xrd_player_finished(p))
            break;

        unsigned tex = xrd_player_texture(p);
        /* After PageDown the texture still holds the old photo until the new
         * one is drawn; those frames are not the new photo's. */
        const bool shown = tex && xrd_player_file_shown(p);
        if (shown) {
            rendered = true;
            depth_fits(&app);
        }

        if (size_dirty && !eye_fbo) { /* the one-eye target keeps its size */
            SDL_GL_GetDrawableSize(win, &out_w, &out_h);
            if (out_w < 2)
                out_w = 2;
            if (out_h < 1)
                out_h = 1;
            win_w = out_w;
            win_h = out_h;
        }
        size_dirty = false;
        /* geometry, the mipmap decision and the texture size for this frame */
        if (tex && update_view(&app, out_w, out_h) < 0) {
            fail("%s", xrd_player_error(p));
            goto out;
        }

        if (tex && !hwdec_checked) {
            const char *hw = xrd_player_hwdec(p);
            fprintf(stderr, "hwdec: %s\n", hw[0] ? hw : "unknown");
            hwdec_checked = true;
            fprintf(stderr, "mpv: profile=%s", cfg.mpv_profile);
            for (int i = 0; i < cfg.mpv_opt_count; i++)
                fprintf(stderr, " %s", cfg.mpv_opts[i]);
            fputc('\n', stderr);
            print_texture(&app);
            xrd_player_texture_size(p, &shown_w, &shown_h);
            if (!xrd_hwdec_is_zero_copy(hw)) {
                /* "not vaapi" on Linux by default: the smoke tests match it. */
                snprintf(err, sizeof err, "hardware decoding is '%s', not %s",
                         hw[0] ? hw : "unknown", hwdec);
                const char *note = xrd_platform_hwdec_note(plat);
                if (cfg.require_hwdec) {
                    char why[sizeof err + 160];
                    snprintf(why, sizeof why, "%s (--require-hwdec)%s%s", err,
                             note[0] ? "; " : "", note);
                    fail("%s", why);
                    goto out;
                }
                /* not zero-copy, yet a decoder: a "-copy" variant */
                bool copy = hw[0] && strcmp(hw, "no") != 0;
                fprintf(stderr, "xrdome: warning: %s; decoding %s\n", err,
                        copy ? "in hardware through a copy" : "in software");
                if (note[0])
                    fprintf(stderr, "xrdome: note: %s\n", note);
            }
        } else if (tex && cfg.mpv_scale[0]) {
            int tw, th;
            xrd_player_texture_size(p, &tw, &th);
            if (tw != shown_w || th != shown_h) {
                print_texture(&app);
                shown_w = tw;
                shown_h = th;
            }
        }

        /* Video frames to the depth helper, before the pose sample: never waits */
        video_step(&app, timing, tex);

        glBindFramebuffer(GL_FRAMEBUFFER, eye_fbo); /* 0, the window, without --window-eye */
        glViewport(0, 0, out_w, out_h);

        /* The pose is sampled as late as possible: right before the eye pass. */
        uint64_t pose_sample_ns = xrd_now_ns();
        xrd_quat view = xrd_tracker_view(trk, pose_sample_ns);
        if (mouse_look)
            view = xrd_quat_mul(view, xrd_controls_mouse_quat(&ctl));
        /* A head-locked flat screen ignores the tracker and the mouse (spec 5.4). */
        if (ctl.screen_locked && ctl.proj == XRD_PROJ_FLAT)
            view = XRD_QUAT_IDENTITY;
        if (tex) {
            float rot[9];
            xrd_quat_to_mat3(view, rot);
            xrd_renderer_set_convergence(r, ctl.convergence_deg);
            xrd_renderer_set_filter(r, ctl.filter);
            xrd_renderer_set_sharpen(r, ctl.sharpen);
            const xrd_draw_params dp = {
                .proj = ctl.proj,
                .layout = ctl.layout,
                .swap_eyes = ctl.swap_eyes,
                .geom = app.geom,
                .trilinear = app.trilinear,
                .screen_curve = xrd_controls_screen_curve(&ctl),
                /* the depth belongs to the photo now open: not to the old
                 * picture still up after PageDown */
                .depth = ctl.depth_on && shown && app.depth_w > 0,
                .depth_strength = ctl.depth_strength,
                .depth_plane = ctl.depth_plane,
            };
            /* --mipmap-check: every draw that wants mipmaps must find a chain
             * generated for the frame in the texture. Without one the draw
             * falls back to level 0 (trilinear needs xrd_player_mipmapped),
             * so this checks the player's decisions, not the fallback. */
            if (app.mip_want && cfg.mipmap_check) {
                int vw, vh;
                xrd_player_texture_size(p, &vw, &vh);
                if (!dp.trilinear) {
                    fprintf(stderr, "xrdome: --mipmap-check: trilinear draw %d samples a %dx%d "
                                    "texture whose mipmaps were not generated for its frame\n",
                            mip_draws + 1, vw, vh);
                    goto out;
                }
                if (vw != mip_w || vh != mip_h) {
                    size_t n = strlen(mip_sizes);
                    snprintf(mip_sizes + n, sizeof mip_sizes - n, "%s%dx%d", n ? " then " : "",
                             vw, vh);
                    mip_w = vw;
                    mip_h = vh;
                }
                mip_draws++;
            }
            xrd_renderer_draw(r, tex, rot, &dp, out_w, out_h);
        } else {
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        if (eye_fbo) { /* --window-eye: copy the chosen eye's half to the window, 1:1 */
            int x0 = cfg.window_eye == XRD_WINDOW_EYE_RIGHT ? out_w / 2 : 0;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, eye_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(x0, 0, x0 + out_w / 2, out_h, 0, 0, win_w, win_h,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        bool last = false;
        if (cfg.frames > 0 && shown) {
            presented++;
            if (press_next < press_n) {
                if (presented >= XRD_PRESS_GAP) { /* --press: the next key, as if typed */
                    SDL_Event kev;
                    memset(&kev, 0, sizeof kev);
                    kev.type = SDL_KEYDOWN;
                    kev.key.state = SDL_PRESSED;
                    kev.key.keysym.sym = press_keys[press_next++];
                    SDL_PushEvent(&kev);
                    presented = 0;
                }
            } else if (presented >= cfg.frames) {
                last = true;
                if (cfg.dump_frame[0] && dump_ppm(cfg.dump_frame, win_w, win_h) != 0) {
                    fail("cannot write %s", cfg.dump_frame);
                    goto out;
                }
            }
        }

        /* Nothing between the timestamp and the swap; the log write follows. */
        uint64_t pre_swap_ns = xrd_now_ns();
        SDL_GL_SwapWindow(win);
        if (cfg.sync_after_swap)
            glFinish(); /* diagnostic: wait here for a driver that blocks on vsync later */
        uint64_t post_swap_ns = xrd_now_ns();
        xrd_player_report_swap(p);
        if (tex)
            app.frames++;
        if (tex) /* timing starts with the first video frame, like --frames */
            xrd_timing_frame(timing, pose_sample_ns, pre_swap_ns, xrd_tracker_last_pose_ns(trk),
                             post_swap_ns - pre_swap_ns);
        if (last)
            quit = done = true;

        uint64_t now = xrd_now_ns();
        uint64_t pace = xrd_pace_sleep_ns(prev_swap_ns, now, period_ns);
        xrd_sleep_ns(pace);
        prev_swap_ns = now + pace;
    }

    if (cfg.mipmap_check)
        fprintf(stderr, "mipmap check: %d trilinear draws, texture %s\n", mip_draws,
                mip_sizes[0] ? mip_sizes : "none");
    if (cfg.frames > 0 && presented < cfg.frames && !done) {
        snprintf(err, sizeof err, "--frames %d: exited after %d presented frames", cfg.frames,
                 presented);
        fail("%s", err);
        goto out;
    }
    rc = 0;

out:
    if (mem.on && !mem.block_save && rendered)
        memory_save(&mem, &cfg, cfg.file, &ctl, p ? xrd_player_position(p) : -1.0,
                    p ? xrd_player_duration(p) : -1.0);
    if (pose && strcmp(pose->name, "udp-opentrack") == 0)
        fprintf(stderr, "pose: %u malformed UDP packets dropped\n",
                (unsigned)xrd_pose_udp_dropped(pose));
    video_stop(&app);         /* needs the GL context current */
    free(app.vdepth);
    xrd_timing_close(timing); /* prints the missed-vsync summary */
    xrd_player_free(p);       /* needs the GL context current */
    xrd_imagedir_free(&app.photos);
    xrd_depthclient_free(app.dc); /* a helper it started keeps running, warm, until idle */
    xrd_renderer_free(r);
    if (gl) {
        if (eye_fbo)
            glDeleteFramebuffers(1, &eye_fbo);
        if (eye_tex)
            glDeleteTextures(1, &eye_tex);
        SDL_GL_DeleteContext(gl);
    }
    if (win)
        SDL_DestroyWindow(win);
    /* Switch back only what xrdome switched. Glasses that went away come back
     * in 1080p by themselves; xrd_sbs_restore also skips a driver that is no
     * longer running, whose next start would read the request. */
    if (sbs_switched && !glasses_gone) {
        char why[512];
        if (xrd_sbs_restore((long long)time(NULL), why, sizeof why))
            fprintf(stderr, "display: asked XRLinuxDriver to switch the glasses back to 1080p\n");
        else
            fprintf(stderr, "xrdome: warning: the glasses stay in SBS: %s\n", why);
    }
    SDL_Quit();
    xrd_tracker_free(trk);
    xrd_pose_source_destroy(pose);
    return rc;
}
