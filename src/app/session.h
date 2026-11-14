#ifndef XRD_APP_SESSION_H
#define XRD_APP_SESSION_H

/* Pure: turns the config, the saved record and the detection result into the
 * starting view state, and the view state back into record fields. No SDL, GL
 * or mpv; main.c only wires. */

#include "app/controls.h"
#include "config.h"
#include "detect/detect.h"
#include "store/filestate.h"

typedef struct {
    xrd_file_settings s;        /* every field filled: proj, zoom (VR180), flat_zoom, layout,
                                   packing, zoom_mode, swap, content fov, convergence */
    unsigned explicit_flags;    /* cfg->explicit_flags with XRD_SET_ZOOM moved to
                                   XRD_SET_FLAT_ZOOM when the file opens flat */
    unsigned applied;           /* record bits actually used, for "settings: restored" */
} xrd_session;

/* What the flags and the record state for detection. rec may be NULL (then
 * *record states nothing). A record always states its projection (a record
 * without a proj line is an older, hequirect one). */
void xrd_session_given(const xrd_config *cfg, const xrd_filestate *rec,
                       xrd_detect_given *flags, xrd_detect_given *record);

/* Starting state. Order per value: defaults and device, ini, record (rec may be
 * NULL), flags. proj, layout and packing come from d. --zoom applies to the
 * projection in d->proj; the other projection starts from cfg->zoom_default or
 * cfg->flat_zoom, then the record. */
void xrd_session_start(const xrd_config *cfg, const xrd_filestate *rec, const xrd_detect *d,
                       xrd_session *out);

/* The view state as record fields (zoom and flat_zoom taken from zoom and
 * zoom_other according to proj). */
void xrd_session_settings(const xrd_controls *c, float content_hfov, float content_vfov,
                          xrd_file_settings *out);

/* Controls for a session: xrd_controls_init, _init_display and _init_format. */
void xrd_session_controls(const xrd_session *ss, const xrd_config *cfg, xrd_controls *c);

#endif
