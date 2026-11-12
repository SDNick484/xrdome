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

/* Spec 6.4 keys. Returns true when the key asks to quit. */
static bool handle_key(const SDL_KeyboardEvent *k, xrd_controls *ctl, xrd_tracker *trk,
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
    case SDLK_s:
        if (!k->repeat)
            xrd_controls_toggle_swap(ctl);
        break;
    case SDLK_m:
        if (!k->repeat)
            xrd_controls_cycle_layout(ctl);
        break;
    default:
        break;
    }
    return false;
}

int main(int argc, char **argv)
{
    char err[512] = "";
    char ini[4096];
    xrd_config cfg;
    bool have_ini = xrd_config_default_path(ini, sizeof ini) == 0;
    if (xrd_config_load(&cfg, have_ini ? ini : NULL, argc, argv, err, sizeof err) != 0)
        return fail("%s", err);

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

    int out_w, out_h;
    SDL_GL_GetDrawableSize(win, &out_w, &out_h);

    r = xrd_renderer_new(err, sizeof err);
    if (!r) {
        fail("renderer: %s", err);
        goto out;
    }
    p = xrd_player_open(cfg.file, gl_get_proc, wm.info.x11.display, err, sizeof err);
    if (!p) {
        fail("%s", err);
        goto out;
    }
    if (cfg.frames > 0)
        xrd_player_hold_at_end(p);

    xrd_controls ctl;
    xrd_controls_init(&ctl, cfg.zoom, cfg.layout);
    const bool mouse_look = strcmp(pose->name, "null") == 0;
    const float eye_aspect = (out_w / 2.0f) / (float)out_h;
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
        if (tex && !hwdec_checked) {
            const char *hw = xrd_player_hwdec(p);
            fprintf(stderr, "hwdec: %s\n", hw[0] ? hw : "unknown");
            hwdec_checked = true;
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
        if (tex) {
            float rot[9], tan_h, tan_v;
            xrd_quat_to_mat3(view, rot);
            xrd_fov_tan_half(cfg.fov_diag, eye_aspect, ctl.zoom, &tan_h, &tan_v);
            xrd_renderer_draw(r, tex, rot, tan_h, tan_v, ctl.layout, ctl.swap_eyes,
                              out_w, out_h);
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
        xrd_player_report_swap(p);
        if (tex) /* timing starts with the first video frame, like --frames */
            xrd_timing_frame(timing, pose_sample_ns, pre_swap_ns, xrd_tracker_last_pose_ns(trk));
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
