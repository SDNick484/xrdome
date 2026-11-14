#define _POSIX_C_SOURCE 200809L

#include "display/sbs.h"

#include <SDL.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

xrd_sbs_verdict xrd_sbs_decide(const xrd_xrdriver_state *st, long long now)
{
    if (!st)
        return XRD_SBS_NO_DRIVER;
    if (!xrd_xrdriver_fresh(st, now))
        return XRD_SBS_STALE;
    if (!st->have_sbs)
        return XRD_SBS_NO_GLASSES;
    if (!st->sbs_supported)
        return XRD_SBS_UNSUPPORTED;
    if (st->sbs_enabled)
        return XRD_SBS_ALREADY_ON;
    return XRD_SBS_GO;
}

void xrd_sbs_reason(xrd_sbs_verdict v, const xrd_xrdriver_state *st, long long now,
                    const char *state_path, char *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    switch (v) {
    case XRD_SBS_GO:
        snprintf(buf, len, "XRLinuxDriver can switch the glasses to SBS");
        break;
    case XRD_SBS_NO_DRIVER:
        snprintf(buf, len, "XRLinuxDriver is not running (cannot read %s)", state_path);
        break;
    case XRD_SBS_STALE:
        if (st && st->have_heartbeat)
            snprintf(buf, len, "XRLinuxDriver is not running (the heartbeat in %s is %lld s old)",
                     state_path, now - st->heartbeat);
        else
            snprintf(buf, len, "XRLinuxDriver is not running (no heartbeat in %s)", state_path);
        break;
    case XRD_SBS_NO_GLASSES:
        snprintf(buf, len, "XRLinuxDriver reports no glasses connected");
        break;
    case XRD_SBS_UNSUPPORTED:
        snprintf(buf, len, "XRLinuxDriver reports that these glasses cannot switch to SBS");
        break;
    case XRD_SBS_ALREADY_ON:
        snprintf(buf, len, "XRLinuxDriver reports the glasses already in SBS mode");
        break;
    default:
        snprintf(buf, len, "unknown reason");
        break;
    }
}

/* ---- SDL ---- */

static int sdl_enumerate(void *ctx, xrd_output_info *outs, int max)
{
    (void)ctx;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return -1;
    return xrd_output_enumerate(outs, max);
}

static bool sdl_wait(void *ctx, int ms)
{
    (void)ctx;
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) { /* a failed re-init: wait without events */
        SDL_Delay((Uint32)ms);
        return true;
    }
    SDL_Event ev;
    if (!SDL_WaitEventTimeout(&ev, ms))
        return true;
    do {
        if (ev.type == SDL_QUIT)
            return false;
    } while (SDL_PollEvent(&ev));
    return true;
}

static uint64_t sdl_now_ms(void *ctx)
{
    (void)ctx;
    return SDL_GetTicks64();
}

static long long wall_s(void *ctx)
{
    (void)ctx;
    return (long long)time(NULL);
}

const xrd_sbs_ops *xrd_sbs_sdl_ops(void)
{
    static const xrd_sbs_ops ops = {sdl_enumerate, sdl_wait, sdl_now_ms, wall_s};
    return &ops;
}

/* ---- switch and restore ---- */

int xrd_sbs_switch(const xrd_sbs_ops *ops, void *ctx, const char *want, int timeout_ms,
                   xrd_output_info *outs, int max, int *n, int *idx, xrd_sbs_result *res)
{
    memset(res, 0, sizeof *res);
    int sel = xrd_output_select(outs, *n, want, idx);

    char state[XRD_XRDRIVER_PATH_MAX], control[XRD_XRDRIVER_PATH_MAX];
    if (xrd_xrdriver_path(XRD_XRDRIVER_STATE, state, sizeof state) != 0 ||
        xrd_xrdriver_path(XRD_XRDRIVER_CONTROL, control, sizeof control) != 0) {
        snprintf(res->why, sizeof res->why, "the XR driver directory name is too long");
        return sel;
    }
    xrd_xrdriver_state st;
    bool have = xrd_xrdriver_read_state(state, &st) == 0;
    long long now = ops->epoch_s(ctx);
    xrd_sbs_verdict v = xrd_sbs_decide(have ? &st : NULL, now);
    if (v != XRD_SBS_GO) {
        xrd_sbs_reason(v, have ? &st : NULL, now, state, res->why, sizeof res->why);
        return sel;
    }

    if (xrd_xrdriver_request(control, xrd_xrdriver_sbs_request(true)) != 0) {
        snprintf(res->why, sizeof res->why, "cannot write %s: %s", control, strerror(errno));
        return sel;
    }

    uint64_t t0 = ops->now_ms(ctx);
    for (;;) {
        uint64_t el = ops->now_ms(ctx) - t0;
        int left = el >= (uint64_t)timeout_ms ? 0 : timeout_ms - (int)el;
        if (left == 0) {
            res->outcome = XRD_SBS_TIMED_OUT;
            snprintf(res->why, sizeof res->why,
                     "asked XRLinuxDriver to switch the glasses to SBS, but no 3840x1080 output "
                     "appeared within %d s", (timeout_ms + 999) / 1000);
            return sel;
        }
        if (!ops->wait(ctx, left < XRD_SBS_POLL_MS ? left : XRD_SBS_POLL_MS)) {
            res->outcome = XRD_SBS_QUIT;
            snprintf(res->why, sizeof res->why,
                     "quit while waiting for the glasses to switch to SBS");
            return sel;
        }
        int k = ops->enumerate(ctx, outs, max);
        if (k < 0)
            continue; /* the display may be mid-change; try again until the deadline */
        *n = k;
        sel = xrd_output_select(outs, *n, want, idx);
        if (sel == XRD_OUT_OK) {
            res->outcome = XRD_SBS_SWITCHED;
            res->waited_ms = ops->now_ms(ctx) - t0;
            return sel;
        }
    }
}

bool xrd_sbs_restore(long long now, char *why, size_t len)
{
    char state[XRD_XRDRIVER_PATH_MAX], control[XRD_XRDRIVER_PATH_MAX];
    if (xrd_xrdriver_path(XRD_XRDRIVER_STATE, state, sizeof state) != 0 ||
        xrd_xrdriver_path(XRD_XRDRIVER_CONTROL, control, sizeof control) != 0) {
        snprintf(why, len, "the XR driver directory name is too long");
        return false;
    }
    xrd_xrdriver_state st;
    bool have = xrd_xrdriver_read_state(state, &st) == 0;
    if (!have || !xrd_xrdriver_fresh(&st, now)) {
        xrd_sbs_reason(have ? XRD_SBS_STALE : XRD_SBS_NO_DRIVER, have ? &st : NULL, now, state,
                       why, len);
        return false;
    }
    if (xrd_xrdriver_request(control, xrd_xrdriver_sbs_request(false)) != 0) {
        snprintf(why, len, "cannot write %s: %s", control, strerror(errno));
        return false;
    }
    return true;
}
