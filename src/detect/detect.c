#include "detect/detect.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


/* ---- file name hints ---------------------------------------------------- */

static bool tok_ci(const char *t, size_t n, const char *word)
{
    if (strlen(word) != n)
        return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)t[i]) != tolower((unsigned char)word[i]))
            return false;
    return true;
}

static bool tok_exact(const char *t, size_t n, const char *word)
{
    return strlen(word) == n && !memcmp(t, word, n);
}

static void put_tok(char dst[16], const char *t, size_t n)
{
    if (n > 15)
        n = 15;
    memcpy(dst, t, n);
    dst[n] = '\0';
}

void xrd_detect_name_hints(const char *path, xrd_detect_given *out,
                           char proj_tok[16], char layout_tok[16], char packing_tok[16])
{
    memset(out, 0, sizeof *out);
    proj_tok[0] = layout_tok[0] = packing_tok[0] = '\0';
    if (!path)
        return;

    const char *slash = strrchr(path, '/');
    const char *s = slash ? slash + 1 : path;
    const char *three_d = NULL;
    size_t three_d_len = 0;

    while (*s) {
        while (*s && !isalnum((unsigned char)*s))
            s++;
        const char *t = s;
        while (*s && isalnum((unsigned char)*s))
            s++;
        size_t n = (size_t)(s - t);
        if (n == 0)
            continue;

        if (!out->have_proj &&
            (tok_ci(t, n, "VR180") || tok_ci(t, n, "180") || tok_ci(t, n, "180x180") ||
             tok_ci(t, n, "VR"))) {
            out->have_proj = true;
            out->proj = XRD_PROJ_HEQUIRECT;
            put_tok(proj_tok, t, n);
        }

        bool sbs = tok_ci(t, n, "SBS") || tok_ci(t, n, "3DH") || tok_exact(t, n, "LR");
        bool hsbs = tok_ci(t, n, "HSBS");
        bool fsbs = tok_ci(t, n, "FSBS") || tok_ci(t, n, "FULLSBS");
        bool rl = tok_exact(t, n, "RL");
        bool tb = tok_exact(t, n, "TB") || tok_exact(t, n, "OU") || tok_ci(t, n, "3DV");
        bool htb = tok_ci(t, n, "HTB") || tok_ci(t, n, "HOU");
        bool mono = tok_ci(t, n, "MONO") || tok_ci(t, n, "2D");

        if (tok_ci(t, n, "3D") && !three_d) {
            three_d = t;
            three_d_len = n;
        }
        if (!out->have_layout && (sbs || hsbs || fsbs || rl || tb || htb || mono)) {
            out->have_layout = true;
            out->layout = (sbs || hsbs || fsbs) ? XRD_LAYOUT_SBS_LR
                        : rl                    ? XRD_LAYOUT_SBS_RL
                        : (tb || htb)           ? XRD_LAYOUT_TB_LR
                                                : XRD_LAYOUT_MONO;
            put_tok(layout_tok, t, n);
        }
        if (!out->have_packing && (hsbs || fsbs || htb)) {
            out->have_packing = true;
            out->packing = (hsbs || htb) ? XRD_PACKING_HALF : XRD_PACKING_FULL;
            put_tok(packing_tok, t, n);
        }
    }
    if (!out->have_layout && three_d) {
        out->have_layout = true;
        out->layout = XRD_LAYOUT_SBS_LR;
        put_tok(layout_tok, three_d, three_d_len);
    }
}

/* ---- shape rules -------------------------------------------------------- */

static bool within(double f, double x, double frac)
{
    return fabs(f / x - 1.0) <= frac + 1e-9;
}

/* "layout only" rows; also the flat rows of the first block. */
static xrd_layout shape_layout(double f, xrd_proj proj)
{
    if (proj == XRD_PROJ_HEQUIRECT) {
        if (f > 0 && within(f, 1.0, 0.02))
            return XRD_LAYOUT_MONO;
        if (f > 0 && within(f, 0.5, 0.02))
            return XRD_LAYOUT_TB_LR;
        return XRD_LAYOUT_SBS_LR;
    }
    if (f >= 3.0)
        return XRD_LAYOUT_SBS_LR;
    if (f > 0 && within(f, 0.889, 0.03))
        return XRD_LAYOUT_TB_LR;
    return XRD_LAYOUT_MONO;
}

/* "projection only" rows. */
static xrd_proj shape_proj(double f, xrd_layout layout)
{
    double x = xrd_layout_is_sbs(layout) ? 2.0 : xrd_layout_is_tb(layout) ? 0.5 : 1.0;
    return within(f, x, 0.02) ? XRD_PROJ_HEQUIRECT : XRD_PROJ_FLAT;
}

void xrd_detect_relayout(double frame_aspect, xrd_proj new_proj,
                         xrd_layout *layout, xrd_packing *packing)
{
    *layout = shape_layout(frame_aspect, new_proj);
    *packing = XRD_PACKING_AUTO;
}

static const char *proj_name(xrd_proj p)
{
    return p == XRD_PROJ_FLAT ? "flat" : "hequirect";
}

static const char *layout_name(xrd_layout l)
{
    switch (l) {
    case XRD_LAYOUT_SBS_LR: return "sbs-lr";
    case XRD_LAYOUT_SBS_RL: return "sbs-rl";
    case XRD_LAYOUT_TB_LR:  return "tb-lr";
    case XRD_LAYOUT_TB_RL:  return "tb-rl";
    default:                return "mono";
    }
}

/* ---- resolve ------------------------------------------------------------ */

static void add_note(xrd_detect *d, const char *fmt, ...)
{
    if (d->note_count >= 3)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->notes[d->note_count], sizeof d->notes[0], fmt, ap);
    va_end(ap);
    d->note_count++;
}

static void metadata_given(const xrd_probe *p, xrd_detect_given *g)
{
    memset(g, 0, sizeof *g);
    if (!p)
        return;
    if (p->spherical != XRD_SPH_NONE) {
        g->have_proj = true;
        g->proj = XRD_PROJ_HEQUIRECT;
    }
    switch (p->stereo) {
    case XRD_STEREO_SBS:
        g->have_layout = true;
        g->layout = p->stereo_inverted ? XRD_LAYOUT_SBS_RL : XRD_LAYOUT_SBS_LR;
        break;
    case XRD_STEREO_TB:
        g->have_layout = true;
        g->layout = p->stereo_inverted ? XRD_LAYOUT_TB_RL : XRD_LAYOUT_TB_LR;
        break;
    case XRD_STEREO_2D:
        g->have_layout = true;
        g->layout = XRD_LAYOUT_MONO;
        break;
    default:
        break;
    }
}

void xrd_detect_resolve(const xrd_detect_given *flags, const xrd_detect_given *record,
                        const xrd_probe *probe, const char *path, xrd_detect *out)
{
    memset(out, 0, sizeof *out);
    out->packing = XRD_PACKING_AUTO;

    xrd_detect_given meta, name;
    char ptok[16], ltok[16], ktok[16];
    metadata_given(probe, &meta);
    xrd_detect_name_hints(path, &name, ptok, ltok, ktok);

    const xrd_detect_given *src[4] = { flags, record, &meta, &name };
    const xrd_detect_src id[4] = { XRD_SRC_FLAG, XRD_SRC_RECORD, XRD_SRC_METADATA, XRD_SRC_NAME };

    bool have_proj = false, have_layout = false, have_packing = false;
    for (int i = 0; i < 4; i++) {
        const xrd_detect_given *g = src[i];
        if (!g)
            continue;
        if (!have_proj && g->have_proj) {
            have_proj = true;
            out->proj = g->proj;
            out->proj_src = id[i];
            if (id[i] == XRD_SRC_NAME)
                strcpy(out->proj_token, ptok);
        }
        if (!have_layout && g->have_layout) {
            have_layout = true;
            out->layout = g->layout;
            out->layout_src = id[i];
            if (id[i] == XRD_SRC_NAME)
                strcpy(out->layout_token, ltok);
        }
        if (!have_packing && g->have_packing && g->packing != XRD_PACKING_AUTO) {
            have_packing = true;
            out->packing = g->packing;
            out->packing_src = id[i];
            if (id[i] == XRD_SRC_NAME)
                strcpy(out->packing_token, ktok);
        }
    }
    if (!have_packing)
        out->packing_src = XRD_SRC_AUTO;

    /* Frame shape: pixel aspect is 1:1 when the container carries stereo metadata. */
    double f = 0;
    if (probe && probe->have_size) {
        double sn = 1, sd = 1;
        if (probe->stereo == XRD_STEREO_NONE && probe->sar_num > 0 && probe->sar_den > 0) {
            sn = probe->sar_num;
            sd = probe->sar_den;
        }
        f = (double)probe->width * sn / ((double)probe->height * sd);
        if (!isfinite(f) || f <= 0)
            f = 0;
    }
    out->frame_aspect = f;

    if (!have_proj || !have_layout) {
        if (f > 0) {
            if (!have_proj && !have_layout) {
                if (within(f, 2.0, 0.02)) {
                    out->proj = XRD_PROJ_HEQUIRECT;
                    out->layout = XRD_LAYOUT_SBS_LR;
                } else {
                    out->proj = XRD_PROJ_FLAT;
                    out->layout = shape_layout(f, XRD_PROJ_FLAT);
                }
            } else if (!have_proj) {
                out->proj = shape_proj(f, out->layout);
            } else {
                out->layout = shape_layout(f, out->proj);
            }
            if (!have_proj) out->proj_src = XRD_SRC_SHAPE;
            if (!have_layout) out->layout_src = XRD_SRC_SHAPE;
        } else {
            /* No frame size: what xrdome did before this feature. */
            if (!have_proj) {
                out->proj = XRD_PROJ_HEQUIRECT;
                out->proj_src = XRD_SRC_FALLBACK;
            }
            if (!have_layout) {
                out->layout = out->proj == XRD_PROJ_FLAT ? XRD_LAYOUT_MONO : XRD_LAYOUT_SBS_LR;
                out->layout_src = XRD_SRC_FALLBACK;
            }
            add_note(out, "could not detect the video format (no frame size); "
                          "assuming %s %s", proj_name(out->proj),
                     layout_name(out->layout));
        }
    }

    if (probe) {
        if (probe->stereo == XRD_STEREO_OTHER)
            add_note(out, "stereo 3D type \"%s\" is not supported and was ignored",
                     probe->stereo_name);
        if (probe->spherical == XRD_SPH_OTHER && out->proj_src == XRD_SRC_METADATA)
            add_note(out, "spherical projection \"%s\" is not supported and may look distorted",
                     probe->spherical_name);
    }
}

/* ---- describe ----------------------------------------------------------- */

static int put_src(char *buf, size_t len, size_t at, xrd_detect_src s, const char *tok)
{
    switch (s) {
    case XRD_SRC_FLAG:     return snprintf(buf + at, len - at, "flag");
    case XRD_SRC_RECORD:   return snprintf(buf + at, len - at, "record");
    case XRD_SRC_METADATA: return snprintf(buf + at, len - at, "metadata");
    case XRD_SRC_NAME:     return snprintf(buf + at, len - at, "file name \"%s\"", tok);
    case XRD_SRC_SHAPE:    return snprintf(buf + at, len - at, "frame shape");
    case XRD_SRC_AUTO:     return snprintf(buf + at, len - at, "auto");
    case XRD_SRC_FALLBACK: return snprintf(buf + at, len - at, "fallback");
    default:               return snprintf(buf + at, len - at, "unknown");
    }
}

int xrd_detect_describe(const xrd_detect *d, char *buf, size_t len)
{
    if (!d || !buf || len == 0)
        return -1;
    size_t at = 0;
    int n;
#define ADD(call) do { n = (call); if (n < 0 || (size_t)n >= len - at) { buf[0] = '\0'; return -1; } at += (size_t)n; } while (0)
    ADD(snprintf(buf + at, len - at, "proj=%s (", proj_name(d->proj)));
    ADD(put_src(buf, len, at, d->proj_src, d->proj_token));
    ADD(snprintf(buf + at, len - at, ") layout=%s (", layout_name(d->layout)));
    ADD(put_src(buf, len, at, d->layout_src, d->layout_token));
    ADD(snprintf(buf + at, len - at, ")"));
    if (d->proj == XRD_PROJ_FLAT && d->layout != XRD_LAYOUT_MONO) {
        xrd_packing p = d->packing;
        if (p == XRD_PACKING_AUTO) {
            bool full = xrd_layout_is_sbs(d->layout) ? d->frame_aspect >= 3.0
                                                     : (d->frame_aspect > 0 && d->frame_aspect < 1.5);
            p = full ? XRD_PACKING_FULL : XRD_PACKING_HALF;
        }
        ADD(snprintf(buf + at, len - at, " packing=%s (",
                     p == XRD_PACKING_FULL ? "full" : "half"));
        ADD(put_src(buf, len, at, d->packing_src, d->packing_token));
        ADD(snprintf(buf + at, len - at, ")"));
    }
#undef ADD
    return (int)at;
}
