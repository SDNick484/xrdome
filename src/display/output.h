#ifndef XRD_DISPLAY_OUTPUT_H
#define XRD_DISPLAY_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char name[64];
    int x, y, w, h, refresh_hz;
} xrd_output_info;

#define XRD_OUT_OK        0
#define XRD_OUT_NOT_FOUND 1 /* no such name, or auto found no 3840x1080 output */
#define XRD_OUT_NOT_SBS   2 /* named output exists but is not 3840x1080 */

/* SDL2 on X11 names a display "<EDID product name> <diag>\"" (or the RandR
 * output name when EDID is missing), e.g. `VITURE 23"` or `DP-2 14"`.
 *
 * Pick the glasses output. want "auto" or NULL: first 3840x1080 output.
 * Otherwise a name matches when it equals want, or starts with want followed
 * immediately by a space (so "DP-2" matches `DP-2 14"` but "DP" does not).
 * The first match wins. *idx is written only on XRD_OUT_OK. */
int xrd_output_select(const xrd_output_info *outs, int n, const char *want, int *idx);

/* Whether a failed selection (sel from xrd_output_select) is one that
 * switching the glasses to SBS can fix: want "auto" or NULL found no
 * 3840x1080 output, or the first output named want is 1920x1080 (the
 * glasses in 1080p mirror mode). */
bool xrd_output_sbs_candidate(const xrd_output_info *outs, int n, const char *want, int sel);

/* Writes "name (WxH), name (WxH)..." (or "none" when n == 0) into buf, always
 * NUL-terminated, truncating if len is too small. No SDL calls. */
void xrd_output_list(const xrd_output_info *outs, int n, char *buf, size_t len);

/* Fill outs with up to max displays via SDL. Caller must have called
 * SDL_Init(SDL_INIT_VIDEO). Returns count, or negative on SDL error. */
int xrd_output_enumerate(xrd_output_info *outs, int max);

/* Returns a pointer to a static buffer; not thread-safe. */
const char *xrd_output_strerror(int code, const char *want);

#endif
