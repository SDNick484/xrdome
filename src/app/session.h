#ifndef XRD_APP_SESSION_H
#define XRD_APP_SESSION_H

/* Pure: turns the config, the saved record and the detection result into the
 * starting view state, and the view state back into record fields. No SDL, GL
 * or mpv; main.c only wires. */

#include <stdbool.h>

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

/* Resume (--resume). Enabled only when cfg->resume is set and --no-memory is
 * not given. An explicit start is a --mpv-opt whose key is "start". */
bool xrd_session_resume_enabled(const xrd_config *cfg);
bool xrd_session_has_explicit_start(const xrd_config *cfg);
/* True, with *position set, when the file should open at the record's
 * position: resume enabled, the record holds one, no explicit start, and the
 * position passes the start rule of xrd_resume_check (the duration is not yet
 * known; the player checks it when the file loads). */
bool xrd_session_resume_start(const xrd_config *cfg, const xrd_filestate *rec, double *position);
/* What to record at quit. +1: write *out (the position at quit passes
 * xrd_resume_check). 0: write no position (it is near the start or end: a
 * finished film starts from the beginning next time). -1: leave the record's
 * position as loaded (resume not enabled, or the position or duration is
 * unknown, i.e. <= 0). */
int xrd_session_resume_save(const xrd_config *cfg, double position, double duration,
                            double *out);

/* Controls for a session: xrd_controls_init, _init_display and _init_format. */
void xrd_session_controls(const xrd_session *ss, const xrd_config *cfg, xrd_controls *c);

#endif
