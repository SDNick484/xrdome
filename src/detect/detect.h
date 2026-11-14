#ifndef XRD_DETECT_H
#define XRD_DETECT_H

#include <stdbool.h>
#include <stddef.h>

#include "detect/probe.h"
#include "render/projection.h"

typedef enum { XRD_SRC_NONE, XRD_SRC_FLAG, XRD_SRC_RECORD, XRD_SRC_METADATA, XRD_SRC_NAME,
               XRD_SRC_SHAPE, XRD_SRC_AUTO, XRD_SRC_FALLBACK } xrd_detect_src;

typedef struct {               /* what one source states; unset fields are open */
    bool have_proj, have_layout, have_packing;
    xrd_proj proj; xrd_layout layout; xrd_packing packing; /* packing: HALF or FULL */
} xrd_detect_given;

typedef struct {
    xrd_proj proj; xrd_layout layout;
    xrd_packing packing;       /* AUTO unless a source fixed it */
    xrd_detect_src proj_src, layout_src, packing_src; /* packing_src AUTO when packing is AUTO */
    char proj_token[16], layout_token[16], packing_token[16]; /* for XRD_SRC_NAME */
    double frame_aspect;       /* F of spec 5.3; 0 when the size is unknown */
    char notes[3][200]; int note_count; /* text after "xrdome: note: " */
} xrd_detect;

void xrd_detect_name_hints(const char *path, xrd_detect_given *out,
                           char proj_tok[16], char layout_tok[16], char packing_tok[16]);

/* flags and record may be NULL; probe NULL means no probe data. */
void xrd_detect_resolve(const xrd_detect_given *flags, const xrd_detect_given *record,
                        const xrd_probe *probe, const char *path, xrd_detect *out);

/* The "layout only" rows of the shape table for new_proj; used by the P key.
 * With frame_aspect 0: sbs-lr for hequirect, mono for flat. Packing becomes AUTO. */
void xrd_detect_relayout(double frame_aspect, xrd_proj new_proj,
                         xrd_layout *layout, xrd_packing *packing);

/* "proj=flat (frame shape) layout=sbs-lr (file name \"HSBS\") packing=half (...)".
 * packing is included for flat stereo layouts only; AUTO prints the resolved
 * value with source "auto". Returns length or -1. */
int xrd_detect_describe(const xrd_detect *d, char *buf, size_t len);

#endif
