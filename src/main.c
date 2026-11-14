/* xrdome: VR180 side-by-side playback on XR glasses with head tracking. */
#include <SDL.h>
#include <SDL_syswm.h>
#include <errno.h>
#include <epoxy/gl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/controls.h"
#include "config.h"
#include "display/output.h"
#include "math/quat.h"
#include "pose/pose.h"
#include "render/projection.h"
#include "render/renderer_gl.h"
#include "store/filestate.h"
#include "track/tracker.h"
#include "util/timing.h"
#include "video/player_mpv.h"

#define MAX_OUTPUTS 16
#define WINDOWED_W 1280
#define WINDOWED_H 360
#define SEEK_STEP_S 10.0

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

/* Spec 6.4 keys. Returns true when the key asks to quit. */
static bool handle_key_action(const SDL_KeyboardEvent *k, xrd_controls *ctl, xrd_tracker *trk,
                              xrd_player *p)
{
    bool shift = (k->keysym.mod & KMOD_SHIFT) != 0;
    switch (k->keysym.sym) {
    case SDLK_q:
        return true;
    case SDLK_SPACE:
        if (!k->repeat)
            xrd_player_toggle_pause(p);
        break;
    case SDLK_LEFT:
        xrd_player_seek(p, -SEEK_STEP_S);
        break;
    case SDLK_RIGHT:
        xrd_player_seek(p, SEEK_STEP_S);
        break;
    case SDLK_r:
        xrd_tracker_recenter(trk, !shift); /* R: yaw only, Shift+R: full */
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
        if (!k->repeat)
            xrd_controls_cycle_layout(ctl, 0.0); /* frame_aspect arrives with the wiring issue */
        break;
    default:
        break;
    }
    return false;
}

/* handle_key_action, then a status line for each display value it changed. */
static bool handle_key(const SDL_KeyboardEvent *k, xrd_controls *ctl, xrd_tracker *trk,
                       xrd_player *p)
{
    const xrd_controls before = *ctl;
    bool quit = handle_key_action(k, ctl, trk, p);
    report_display_changes(&before, ctl);
    return quit;
}

/* Per-file settings memory (spec 5.4 to 5.8): the record found at start and
 * what decides whether anything is written at quit. */
typedef struct {
    bool on;            /* a key and a state directory exist and --no-memory is not given */
    bool have_loaded;   /* a record was read and parsed */
    bool block_save;    /* the record is unreadable or newer: never overwrite it */
    char key[XRD_FILESTATE_KEY_MAX];
    char dir[4096];
    xrd_filestate loaded;
    xrd_file_settings start; /* the values playback starts with, after the record */
} xrd_memory;

static xrd_file_settings settings_from_config(const xrd_config *cfg)
{
    xrd_file_settings s = {.zoom = cfg->zoom, .layout = cfg->layout, .swap = false,
                           .content_hfov = cfg->content_hfov, .content_vfov = cfg->content_vfov,
                           .convergence = cfg->convergence};
    return s;
}

/* Record bits main.c does not act on yet; the wiring issue (#8) drops this mask so
 * "settings:" lines stay as they were. */
#define XRD_SET_FLAT_BITS (XRD_SET_PROJ | XRD_SET_FLAT_ZOOM | XRD_SET_PACKING | XRD_SET_ZOOM_MODE)

/* Handles --forget and --no-memory, loads the file's record and applies it
 * under the command-line flags into cfg. Silent when the file has no key or
 * there is no state directory. */
static void memory_load_and_apply(xrd_memory *m, xrd_config *cfg)
{
    memset(m, 0, sizeof *m);
    m->start = settings_from_config(cfg);
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

    char path[4200], err[256] = "", text[160];
    if (xrd_filestate_record_path(m->dir, m->key, path, sizeof path) != 0)
        return;
    switch (xrd_filestate_load(path, &m->loaded, err, sizeof err)) {
    case XRD_FS_OK: {
        m->have_loaded = true;
        xrd_file_settings s = settings_from_config(cfg);
        unsigned applied = xrd_filestate_apply(&m->loaded, cfg->explicit_flags, &s) &
                           ~XRD_SET_FLAT_BITS;
        cfg->zoom = s.zoom;
        cfg->layout = s.layout;
        cfg->content_hfov = s.content_hfov;
        cfg->content_vfov = s.content_vfov;
        cfg->convergence = s.convergence;
        m->start = s;
        if (applied && xrd_filestate_describe(&m->loaded, applied, text, sizeof text) > 0)
            fprintf(stderr, "settings: restored %s\n", text);
        break;
    }
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
 * was loaded. Failure only warns; the exit status is not touched. */
static void memory_save(const xrd_memory *m, const xrd_config *cfg, const xrd_controls *ctl)
{
    xrd_file_settings end = {.zoom = ctl->zoom, .layout = ctl->layout, .swap = ctl->swap_eyes,
                             .content_hfov = cfg->content_hfov, .content_vfov = cfg->content_vfov,
                             .convergence = ctl->convergence_deg};
    xrd_filestate out; /* distinct from m->loaded: collect clears it first */
    xrd_filestate_collect(m->have_loaded ? &m->loaded : NULL, cfg->explicit_flags, &m->start,
                          &end, &out);
    if (out.set == 0 && !m->have_loaded)
        return;
    if (m->have_loaded && xrd_filestate_equal(&m->loaded, &out))
        return;

    char err[256] = "", text[160];
    if (xrd_filestate_save(m->dir, m->key, &out, cfg->file, err, sizeof err) != 0)
        fprintf(stderr, "xrdome: warning: cannot save settings: %s\n", err);
    else if (xrd_filestate_describe(&out, out.set & ~XRD_SET_FLAT_BITS, text, sizeof text) > 0)
        fprintf(stderr, "settings: saved %s\n", text);
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
    memory_load_and_apply(&mem, &cfg);

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
                               .fbo_format = cfg.fbo_format};
    p = xrd_player_open_ex(cfg.file, gl_get_proc, wm.info.x11.display, &popt, err, sizeof err);
    if (!p) {
        fail("%s", err);
        goto out;
    }
    xrd_player_set_content(p, cfg.layout, cfg.content_hfov, cfg.content_vfov);
    xrd_renderer_set_content_fov(r, cfg.content_hfov, cfg.content_vfov);
    xrd_renderer_set_dither(r, cfg.dither);
    if (cfg.frames > 0)
        xrd_player_hold_at_end(p);

    xrd_controls_init(&ctl, cfg.zoom, cfg.layout);
    xrd_controls_init_display(&ctl, cfg.convergence, cfg.filter);
    ctl.swap_eyes = mem.start.swap;
    const bool mouse_look = strcmp(pose->name, "null") == 0;
    float eye_aspect = 1.f;
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
                if (handle_key(&ev.key, &ctl, trk, p))
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
        if (tex && !hwdec_checked) {
            const char *hw = xrd_player_hwdec(p);
            fprintf(stderr, "hwdec: %s\n", hw[0] ? hw : "unknown");
            hwdec_checked = true;
            fprintf(stderr, "mpv: profile=%s", cfg.mpv_profile);
            for (int i = 0; i < cfg.mpv_opt_count; i++)
                fprintf(stderr, " %s", cfg.mpv_opts[i]);
            fputc('\n', stderr);
            fprintf(stderr, "video texture: %s dither=%s\n",
                    xrd_texture_bits_name(xrd_player_texture_bits(p)), cfg.dither ? "on" : "off");
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

        if (size_dirty) {
            SDL_GL_GetDrawableSize(win, &out_w, &out_h);
            if (out_w < 2)
                out_w = 2;
            if (out_h < 1)
                out_h = 1;
            eye_aspect = (out_w / 2.0f) / (float)out_h;
            size_dirty = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, out_w, out_h);

        /* The pose is sampled as late as possible: right before the eye pass. */
        uint64_t pose_sample_ns = xrd_now_ns();
        xrd_quat view = xrd_tracker_view(trk, pose_sample_ns);
        if (mouse_look)
            view = xrd_quat_mul(view, xrd_controls_mouse_quat(&ctl));
        if (tex) {
            float rot[9], tan_h, tan_v;
            xrd_quat_to_mat3(view, rot);
            xrd_fov_tan_half(cfg.fov_diag, eye_aspect, ctl.zoom, &tan_h, &tan_v);
            xrd_renderer_set_convergence(r, ctl.convergence_deg);
            xrd_renderer_set_filter(r, ctl.filter);
            const xrd_draw_params dp = {
                .proj = XRD_PROJ_HEQUIRECT,
                .layout = ctl.layout,
                .swap_eyes = ctl.swap_eyes,
                .geom = {.tan_h = tan_h, .tan_v = tan_v},
                .trilinear = false,
            };
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

    if (cfg.frames > 0 && presented < cfg.frames && !done) {
        snprintf(err, sizeof err, "--frames %d: exited after %d presented frames", cfg.frames,
                 presented);
        fail("%s", err);
        goto out;
    }
    rc = 0;

out:
    if (mem.on && !mem.block_save && rendered)
        memory_save(&mem, &cfg, &ctl);
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
