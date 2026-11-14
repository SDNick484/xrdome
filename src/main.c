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

#include "app/controls.h"
#include "app/session.h"
#include "config.h"
#include "detect/detect.h"
#include "detect/probe.h"
#include "display/output.h"
#include "math/quat.h"
#include "pose/pose.h"
#include "render/projection.h"
#include "render/renderer_gl.h"
#include "store/filestate.h"
#include "track/tracker.h"
#include "util/timing.h"
#include "values.h"
#include "video/player_mpv.h"

#define MAX_OUTPUTS 16
#define WINDOWED_W 1280
#define WINDOWED_H 360
#define XRD_PI_D 3.14159265358979323846

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
    unsigned long frames;    /* video frames presented */
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

/* View geometry for the current state and the eye size, and the mipmap
 * decision of spec 5.6: mipmaps while the picture is shrunk at the centre. */
static void update_view(xrd_app *a, int out_w, int out_h)
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
    a->trilinear = want && xrd_player_mipmapped(a->p);
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
    default:               w = "unknown"; break;
    }
    return w;
}

/* The key list (?), in the order of the README's table. */
static void print_keys(void)
{
    static const char *const keys[][2] = {
        {"Space", "pause / resume"},
        {"Left / Right", "seek -10 s / +10 s"},
        {"Up / Down", "seek +60 s / -60 s"},
        {"R", "recenter, yaw only"},
        {"Shift+R", "recenter, full"},
        {"[ / ]", "zoom out / in"},
        {"F", "bicubic / bilinear sampling"},
        {"S", "swap eyes"},
        {"M", "cycle layout (flat: 2D, half SBS, full SBS, half TB, full TB)"},
        {"P", "projection: flat / VR180"},
        {"Z", "zoom mode: screen / magnify (flat)"},
        {"L", "screen: fixed / head-locked (flat)"},
        {"?", "this list"},
        {"B", "debanding on / off"},
        {"I", "playback information"},
        {"Q", "quit"},
    };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        fprintf(stderr, "keys: %s  %s\n", keys[i][0], keys[i][1]);
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
                    2.0 * atan((double)a->geom.half_w) * 180.0 / XRD_PI_D,
                    2.0 * atan((double)a->geom.half_h) * 180.0 / XRD_PI_D);
        else
            fputc('\n', stderr);
        fprintf(stderr, "info: screen: %s\n", c->screen_locked ? "head-locked" : "fixed");
    } else {
        fprintf(stderr, "info: zoom: %.2f\n", (double)c->zoom);
        fprintf(stderr, "info: screen: fixed (VR180)\n");
    }
    fprintf(stderr, "info: filter: %s, mipmaps %s\n",
            a->trilinear ? "trilinear" : c->filter == XRD_FILTER_BICUBIC ? "bicubic" : "bilinear",
            a->mip_want ? "on" : "off");
    fprintf(stderr, "info: texture: %s %dx%d dither=%s\n",
            xrd_texture_bits_name(xrd_player_texture_bits(a->p)), vw, vh,
            a->cfg->dither ? "on" : "off");
    fprintf(stderr, "info: deband: %s\n", xrd_player_deband(a->p) ? "on" : "off");
    fprintf(stderr, "info: frames: %lu presented, %u missed vsyncs\n", a->frames,
            xrd_timing_missed_vsyncs(a->timing));
}

/* Prints the zoom and filter lines for the values that differ
 * between before and after (a key press at a limit prints nothing). */
static void report_display_changes(const xrd_controls *before, const xrd_controls *after)
{
    if (after->zoom != before->zoom)
        fprintf(stderr, "zoom: %.2f\n", (double)after->zoom);
    if (after->filter != before->filter)
        fprintf(stderr, "filter: %s\n",
                after->filter == XRD_FILTER_BICUBIC ? "bicubic" : "bilinear");
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
        (a->det.layout_src == XRD_SRC_SHAPE || a->det.layout_src == XRD_SRC_FALLBACK)) {
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
    switch (k->keysym.sym) {
    case SDLK_q:
        return true;
    case SDLK_SPACE:
        if (!k->repeat)
            xrd_player_toggle_pause(a->p);
        break;
    case SDLK_LEFT:
        xrd_player_seek(a->p, -XRD_SEEK_SMALL_S);
        break;
    case SDLK_RIGHT:
        xrd_player_seek(a->p, XRD_SEEK_SMALL_S);
        break;
    case SDLK_UP:
        xrd_player_seek(a->p, XRD_SEEK_LARGE_S);
        break;
    case SDLK_DOWN:
        xrd_player_seek(a->p, -XRD_SEEK_LARGE_S);
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
        if (!k->repeat)
            xrd_controls_toggle_filter(ctl);
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
static void memory_save(const xrd_memory *m, const char *file, const xrd_controls *ctl)
{
    xrd_file_settings end;
    xrd_session_settings(ctl, m->start.content_hfov, m->start.content_vfov, &end);
    xrd_filestate out; /* distinct from m->loaded: collect clears it first */
    xrd_filestate_collect(m->have_loaded ? &m->loaded : NULL, m->explicit_flags, &m->start,
                          &end, &out);
    if (out.set == 0 && !m->have_loaded)
        return;
    out.set |= XRD_SET_PROJ;
    out.v.proj = end.proj;
    if (m->have_loaded && xrd_filestate_equal(&m->loaded, &out))
        return;

    char err[256] = "", text[200];
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
    char text[200];
    if (rec && ss->applied && xrd_filestate_describe(rec, ss->applied, text, sizeof text) > 0)
        fprintf(stderr, "settings: restored %s\n", text);
    if (a->det.proj == XRD_PROJ_FLAT && (cfg->explicit_flags & XRD_SET_CONTENT_FOV))
        fprintf(stderr, "xrdome: note: --content-fov applies to VR180 only; "
                        "ignored for this flat video\n");

    mem->start = ss->s;
    mem->explicit_flags = ss->explicit_flags;
    a->content_hfov = ss->s.content_hfov;
    a->content_vfov = ss->s.content_vfov;
}

int main(int argc, char **argv)
{
    char err[512] = "";
    char ini[4096];
    xrd_config cfg;
    bool have_ini = xrd_config_default_path(ini, sizeof ini) == 0;
    if (xrd_config_load(&cfg, have_ini ? ini : NULL, argc, argv, err, sizeof err) != 0)
        return fail("%s", err);

    xrd_memory mem;
    memory_load(&mem, &cfg);
    xrd_app app;
    memset(&app, 0, sizeof app);
    app.cfg = &cfg;
    app.mem = &mem;
    xrd_session ss;
    detect_and_start(&app, &cfg, &mem, &ss);

    xrd_pose_source *pose = xrd_pose_source_create(cfg.pose, err, sizeof err);
    if (!pose)
        return fail("%s", err);

    int rc = 1;
    xrd_tracker *trk = NULL;
    xrd_timing *timing = NULL;
    SDL_Window *win = NULL;
    SDL_GLContext gl = NULL;
    xrd_renderer *r = NULL;
    xrd_player *p = NULL;
    int presented = 0; /* frames swapped since the first video frame (--frames) */
    bool done = false; /* --frames reached, or the glasses went away */
    bool rendered = false; /* a video frame was drawn (gates saving the settings) */
    xrd_controls ctl;      /* valid once rendered is set */

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

    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fail("SDL_Init: %s", SDL_GetError());
        goto out;
    }

    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;
    int w = WINDOWED_W, h = WINDOWED_H;
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
        if (sel != XRD_OUT_OK) {
            fail("%s", xrd_output_strerror(sel, cfg.output));
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
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    win = SDL_CreateWindow("xrdome", x, y, w, h, wflags);
    if (!win) {
        fail("SDL_CreateWindow: %s", SDL_GetError());
        goto out;
    }
    gl = SDL_GL_CreateContext(win);
    if (!gl) {
        fail("cannot create a GL 3.3 core context on EGL: %s", SDL_GetError());
        goto out;
    }
    if (SDL_GL_SetSwapInterval(1) != 0)
        fprintf(stderr, "xrdome: warning: cannot enable vsync: %s\n", SDL_GetError());

    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(win, &wm) || wm.subsystem != SDL_SYSWM_X11) {
        fail("%s", "an X11 (Xorg) session is required");
        goto out;
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

    int out_w, out_h;
    SDL_GL_GetDrawableSize(win, &out_w, &out_h);

    r = xrd_renderer_new(err, sizeof err);
    if (!r) {
        fail("renderer: %s", err);
        goto out;
    }
    const char *mpv_opts[sizeof cfg.mpv_opts / sizeof cfg.mpv_opts[0]];
    for (int i = 0; i < cfg.mpv_opt_count; i++)
        mpv_opts[i] = cfg.mpv_opts[i];
    xrd_player_options popt = {.profile = cfg.mpv_profile, .opts = mpv_opts,
                               .opt_count = cfg.mpv_opt_count, .log_level = cfg.mpv_log,
                               .fbo_format = cfg.fbo_format, .deband = cfg.deband};
    p = xrd_player_open_ex(cfg.file, gl_get_proc, wm.info.x11.display, &popt, err, sizeof err);
    if (!p) {
        fail("%s", err);
        goto out;
    }
    xrd_session_controls(&ss, &cfg, &ctl);
    app.ctl = &ctl;
    app.trk = trk;
    app.p = p;
    app.timing = timing;
    apply_format(&app); /* before the first xrd_player_update */
    xrd_renderer_set_content_fov(r, ss.s.content_hfov, ss.s.content_vfov);
    xrd_renderer_set_dither(r, cfg.dither);
    if (cfg.frames > 0)
        xrd_player_hold_at_end(p);
    int mip_draws = 0;      /* --mipmap-check: draws that wanted trilinear sampling */
    char mip_sizes[256] = ""; /* --mipmap-check: texture sizes drawn trilinearly, in order */
    int mip_w = 0, mip_h = 0;

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
                    quit = done = true;
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

        if (xrd_player_update(p) < 0) {
            fail("%s", xrd_player_error(p));
            goto out;
        }
        if (xrd_player_finished(p))
            break;

        unsigned tex = xrd_player_texture(p);
        if (tex)
            rendered = true;

        if (size_dirty) {
            SDL_GL_GetDrawableSize(win, &out_w, &out_h);
            if (out_w < 2)
                out_w = 2;
            if (out_h < 1)
                out_h = 1;
            size_dirty = false;
        }
        if (tex) /* geometry and the mipmap decision for this frame */
            update_view(&app, out_w, out_h);

        if (tex && !hwdec_checked) {
            const char *hw = xrd_player_hwdec(p);
            fprintf(stderr, "hwdec: %s\n", hw[0] ? hw : "unknown");
            hwdec_checked = true;
            fprintf(stderr, "mpv: profile=%s", cfg.mpv_profile);
            for (int i = 0; i < cfg.mpv_opt_count; i++)
                fprintf(stderr, " %s", cfg.mpv_opts[i]);
            fputc('\n', stderr);
            int vw, vh;
            xrd_player_video_size(p, &vw, &vh);
            fprintf(stderr, "video texture: %s %dx%d dither=%s mipmap=%s\n",
                    xrd_texture_bits_name(xrd_player_texture_bits(p)), vw, vh,
                    cfg.dither ? "on" : "off", app.mip_want ? "on" : "off");
            if (strcmp(hw, "vaapi") != 0) {
                snprintf(err, sizeof err, "hardware decoding is '%s', not vaapi",
                         hw[0] ? hw : "unknown");
                if (cfg.require_hwdec) {
                    fail("%s (--require-hwdec)", err);
                    goto out;
                }
                fprintf(stderr, "xrdome: warning: %s; decoding in software\n", err);
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
            const xrd_draw_params dp = {
                .proj = ctl.proj,
                .layout = ctl.layout,
                .swap_eyes = ctl.swap_eyes,
                .geom = app.geom,
                .trilinear = app.trilinear,
            };
            /* --mipmap-check: every draw that wants mipmaps must find a chain
             * generated for the frame in the texture. Without one the draw
             * falls back to level 0 (trilinear needs xrd_player_mipmapped),
             * so this checks the player's decisions, not the fallback. */
            if (app.mip_want && cfg.mipmap_check) {
                int vw, vh;
                xrd_player_video_size(p, &vw, &vh);
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

        bool last = false;
        if (cfg.frames > 0 && tex && ++presented >= cfg.frames) {
            last = true;
            if (cfg.dump_frame[0] && dump_ppm(cfg.dump_frame, out_w, out_h) != 0) {
                fail("cannot write %s", cfg.dump_frame);
                goto out;
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
        memory_save(&mem, cfg.file, &ctl);
    if (pose && strcmp(pose->name, "udp-opentrack") == 0)
        fprintf(stderr, "pose: %u malformed UDP packets dropped\n",
                (unsigned)xrd_pose_udp_dropped(pose));
    xrd_timing_close(timing); /* prints the missed-vsync summary */
    xrd_player_free(p);       /* needs the GL context current */
    xrd_renderer_free(r);
    if (gl)
        SDL_GL_DeleteContext(gl);
    if (win)
        SDL_DestroyWindow(win);
    SDL_Quit();
    xrd_tracker_free(trk);
    xrd_pose_source_destroy(pose);
    return rc;
}
