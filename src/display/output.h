#ifndef XRD_DISPLAY_OUTPUT_H
#define XRD_DISPLAY_OUTPUT_H

typedef struct {
    char name[64];
    int x, y, w, h, refresh_hz;
} xrd_output_info;

#define XRD_OUT_OK        0
#define XRD_OUT_NOT_FOUND 1 /* no such name, or auto found no 3840x1080 output */
#define XRD_OUT_NOT_SBS   2 /* named output exists but is not 3840x1080 */

/* Pick the glasses output. want "auto" or NULL: first 3840x1080 output.
 * Otherwise exact name match. *idx is written only on XRD_OUT_OK. */
int xrd_output_select(const xrd_output_info *outs, int n, const char *want, int *idx);

/* Fill outs with up to max displays via SDL. Caller must have called
 * SDL_Init(SDL_INIT_VIDEO). Returns count, or negative on SDL error. */
int xrd_output_enumerate(xrd_output_info *outs, int max);

/* Returns a pointer to a static buffer; not thread-safe. */
const char *xrd_output_strerror(int code, const char *want);

#endif
