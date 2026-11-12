/* xrdome: VR180 side-by-side playback on XR glasses.
 * Task 8: fixed (identity) orientation. Head tracking comes in Task 9. */
#include <SDL.h>
#include <SDL_syswm.h>
#include <epoxy/gl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "display/output.h"
#include "math/quat.h"
#include "render/projection.h"
#include "render/renderer_gl.h"
#include "video/player_mpv.h"

#define MAX_OUTPUTS 16
#define WINDOWED_W 1280
#define WINDOWED_H 360

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

int main(int argc, char **argv)
{
    char err[512] = "";
    char ini[4096];
    xrd_config cfg;
    bool have_ini = xrd_config_default_path(ini, sizeof ini) == 0;
    if (xrd_config_load(&cfg, have_ini ? ini : NULL, argc, argv, err, sizeof err) != 0)
        return fail("%s", err);
    /* cfg.pose is parsed but unused until head tracking (Task 9). */

    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return fail("SDL_Init: %s", SDL_GetError());

    int rc = 1;
    SDL_Window *win = NULL;
    SDL_GLContext gl = NULL;
    xrd_renderer *r = NULL;
    xrd_player *p = NULL;

    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;
    int w = WINDOWED_W, h = WINDOWED_H;
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

    float tan_h, tan_v;
    xrd_fov_tan_half(cfg.fov_diag, (out_w / 2.0f) / (float)out_h, cfg.zoom, &tan_h, &tan_v);
    float rot[9];
    xrd_quat_to_mat3(XRD_QUAT_IDENTITY, rot);

    bool quit = false, hwdec_reported = false;
    int presented = 0; /* frames swapped since the first video frame (--frames) */
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) ||
                (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_q))
                quit = true;
        }

        if (xrd_player_update(p) < 0) {
            fail("%s", xrd_player_error(p));
            goto out;
        }
        if (xrd_player_finished(p))
            break;

        unsigned tex = xrd_player_texture(p);
        if (tex && !hwdec_reported) {
            const char *hw = xrd_player_hwdec(p);
            fprintf(stderr, "hwdec: %s\n", hw[0] ? hw : "unknown");
            hwdec_reported = true;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, out_w, out_h);
        if (tex) {
            xrd_renderer_draw(r, tex, rot, tan_h, tan_v, cfg.layout, false, out_w, out_h);
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

        SDL_GL_SwapWindow(win);
        xrd_player_report_swap(p);
        if (last)
            quit = true;
    }
    rc = 0;

out:
    xrd_player_free(p); /* needs the GL context current */
    xrd_renderer_free(r);
    if (gl)
        SDL_GL_DeleteContext(gl);
    if (win)
        SDL_DestroyWindow(win);
    SDL_Quit();
    return rc;
}
