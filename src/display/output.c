#include "display/output.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define XRD_SBS_W 3840
#define XRD_SBS_H 1080

static int is_sbs(const xrd_output_info *o)
{
    return o->w == XRD_SBS_W && o->h == XRD_SBS_H;
}

/* SDL names are "<product or RandR name> <diag>\"": match whole or up to a space. */
static bool name_matches(const char *name, const char *want)
{
    size_t n = strlen(want);
    return strncmp(name, want, n) == 0 && (name[n] == '\0' || name[n] == ' ');
}

int xrd_output_select(const xrd_output_info *outs, int n, const char *want, int *idx)
{
    int automatic = want == NULL || strcmp(want, "auto") == 0;

    for (int i = 0; i < n; i++) {
        if (automatic) {
            if (is_sbs(&outs[i])) {
                *idx = i;
                return XRD_OUT_OK;
            }
        } else if (name_matches(outs[i].name, want)) {
            if (!is_sbs(&outs[i]))
                return XRD_OUT_NOT_SBS;
            *idx = i;
            return XRD_OUT_OK;
        }
    }
    return XRD_OUT_NOT_FOUND;
}

void xrd_output_list(const xrd_output_info *outs, int n, char *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    buf[0] = '\0';
    if (n <= 0) {
        snprintf(buf, len, "none");
        return;
    }
    size_t used = 0;
    for (int i = 0; i < n && used < len - 1; i++) {
        int w = snprintf(buf + used, len - used, "%s%s (%dx%d)", i ? ", " : "",
                         outs[i].name, outs[i].w, outs[i].h);
        if (w < 0)
            break;
        used += (size_t)w;
    }
}

int xrd_output_enumerate(xrd_output_info *outs, int max)
{
    int n = SDL_GetNumVideoDisplays();
    if (n < 0)
        return -1;
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++) {
        SDL_DisplayMode mode;
        SDL_Rect bounds;
        const char *name = SDL_GetDisplayName(i);
        if (SDL_GetCurrentDisplayMode(i, &mode) != 0 ||
            SDL_GetDisplayBounds(i, &bounds) != 0)
            return -1;
        snprintf(outs[i].name, sizeof outs[i].name, "%s", name ? name : "");
        outs[i].x = bounds.x;
        outs[i].y = bounds.y;
        outs[i].w = bounds.w;
        outs[i].h = bounds.h;
        outs[i].refresh_hz = mode.refresh_rate;
    }
    return n;
}

const char *xrd_output_strerror(int code, const char *want)
{
    static char buf[256]; /* not thread-safe */
    int automatic = want == NULL || strcmp(want, "auto") == 0;

    switch (code) {
    case XRD_OUT_OK:
        return "ok";
    case XRD_OUT_NOT_FOUND:
        if (automatic)
            snprintf(buf, sizeof buf,
                     "no 3840x1080 output was found; switch the glasses to SBS mode");
        else
            snprintf(buf, sizeof buf, "no output named \"%s\" was found", want);
        return buf;
    case XRD_OUT_NOT_SBS:
        snprintf(buf, sizeof buf,
                 "output \"%s\" is not 3840x1080; switch the glasses to SBS mode",
                 want ? want : "");
        return buf;
    default:
        return "unknown output error";
    }
}
