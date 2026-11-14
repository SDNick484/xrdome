#include "app/session.h"

#include <string.h>

void xrd_session_given(const xrd_config *cfg, const xrd_filestate *rec,
                       xrd_detect_given *flags, xrd_detect_given *record)
{
    memset(flags, 0, sizeof *flags);
    memset(record, 0, sizeof *record);

    flags->have_proj = !cfg->proj_auto;
    flags->proj = cfg->proj;
    flags->have_layout = !cfg->layout_auto;
    flags->layout = cfg->layout;
    flags->have_packing = cfg->packing != XRD_PACKING_AUTO;
    flags->packing = cfg->packing;

    if (!rec)
        return;
    record->have_proj = true;
    record->proj = (rec->set & XRD_SET_PROJ) ? rec->v.proj : XRD_PROJ_HEQUIRECT;
    if (rec->set & XRD_SET_LAYOUT) {
        record->have_layout = true;
        record->layout = rec->v.layout;
    }
    if (rec->set & XRD_SET_PACKING) {
        record->have_packing = true;
        record->packing = rec->v.packing;
    }
}

void xrd_session_start(const xrd_config *cfg, const xrd_filestate *rec, const xrd_detect *d,
                       xrd_session *out)
{
    memset(out, 0, sizeof *out);
    xrd_file_settings *s = &out->s;
    bool flat = d->proj == XRD_PROJ_FLAT;

    /* Defaults, device and ini. */
    s->zoom = cfg->zoom_default;
    s->flat_zoom = cfg->flat_zoom;
    s->swap = false;
    s->content_hfov = cfg->content_hfov;
    s->content_vfov = cfg->content_vfov;
    s->convergence = cfg->convergence;
    s->zoom_mode = cfg->zoom_mode;

    /* --zoom belongs to the projection the file opens in. */
    out->explicit_flags = cfg->explicit_flags;
    if (flat && (out->explicit_flags & XRD_SET_ZOOM)) {
        out->explicit_flags &= ~(unsigned)XRD_SET_ZOOM;
        out->explicit_flags |= XRD_SET_FLAT_ZOOM;
    }

    /* Record, then flags (which the record never overrides). */
    if (rec)
        out->applied = xrd_filestate_apply(rec, out->explicit_flags, s);
    if (cfg->explicit_flags & XRD_SET_ZOOM) {
        if (flat)
            s->flat_zoom = cfg->zoom;
        else
            s->zoom = cfg->zoom;
    }

    /* Projection, layout and packing were already merged by detection. */
    s->proj = d->proj;
    s->layout = d->layout;
    s->packing = d->packing;
}

void xrd_session_settings(const xrd_controls *c, float content_hfov, float content_vfov,
                          xrd_file_settings *out)
{
    memset(out, 0, sizeof *out);
    bool flat = c->proj == XRD_PROJ_FLAT;
    out->proj = c->proj;
    out->zoom = flat ? c->zoom_other : c->zoom;
    out->flat_zoom = flat ? c->zoom : c->zoom_other;
    out->layout = c->layout;
    out->packing = c->packing;
    out->zoom_mode = c->zoom_mode;
    out->swap = c->swap_eyes;
    out->content_hfov = content_hfov;
    out->content_vfov = content_vfov;
    out->convergence = c->convergence_deg;
}

void xrd_session_controls(const xrd_session *ss, const xrd_config *cfg, xrd_controls *c)
{
    const xrd_file_settings *s = &ss->s;
    bool flat = s->proj == XRD_PROJ_FLAT;
    xrd_controls_init(c, flat ? s->flat_zoom : s->zoom, s->layout);
    xrd_controls_init_display(c, s->convergence, cfg->filter);
    xrd_controls_init_format(c, s->proj, s->packing, flat ? s->zoom : s->flat_zoom, s->zoom_mode,
                             cfg->screen_locked);
    c->swap_eyes = s->swap;
}

bool xrd_session_resume_enabled(const xrd_config *cfg)
{
    return cfg->resume && !cfg->no_memory;
}

bool xrd_session_has_explicit_start(const xrd_config *cfg)
{
    for (int i = 0; i < cfg->mpv_opt_count; i++) {
        if (strncmp(cfg->mpv_opts[i], "start=", 6) == 0)
            return true;
    }
    return false;
}

bool xrd_session_resume_start(const xrd_config *cfg, const xrd_filestate *rec, double *position)
{
    if (!rec || !(rec->set & XRD_SET_POSITION) || !xrd_session_resume_enabled(cfg) ||
        xrd_session_has_explicit_start(cfg))
        return false;
    if (xrd_resume_check(rec->v.position, 0.0) != XRD_RESUME_OK)
        return false;
    *position = rec->v.position;
    return true;
}

int xrd_session_resume_save(const xrd_config *cfg, double position, double duration,
                            double *out)
{
    if (!xrd_session_resume_enabled(cfg) || !(position >= 0.0) || !(duration > 0.0))
        return -1;
    if (xrd_resume_check(position, duration) != XRD_RESUME_OK)
        return 0;
    *out = position;
    return 1;
}
