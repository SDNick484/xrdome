#ifndef XRD_FILESTATE_H
#define XRD_FILESTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "render/projection.h"

enum { XRD_SET_ZOOM = 1u << 0, XRD_SET_LAYOUT = 1u << 1, XRD_SET_SWAP = 1u << 2,
       XRD_SET_CONTENT_FOV = 1u << 3, XRD_SET_CONVERGENCE = 1u << 4,
       XRD_SET_PROJ = 1u << 5, XRD_SET_FLAT_ZOOM = 1u << 6, XRD_SET_PACKING = 1u << 7,
       XRD_SET_ZOOM_MODE = 1u << 8 };

typedef struct {
    float zoom;
    xrd_layout layout;
    bool swap;
    float content_hfov, content_vfov;
    float convergence;
    xrd_proj proj;
    float flat_zoom;
    xrd_packing packing;
    xrd_zoom_mode zoom_mode;
} xrd_file_settings;

typedef struct {
    unsigned set;        /* XRD_SET_* bits: which fields of v are meaningful */
    xrd_file_settings v;
} xrd_filestate;

#define XRD_FNV1A64_INIT 0xcbf29ce484222325ULL
#define XRD_FILESTATE_KEY_MAX 40
#define XRD_FILESTATE_CHUNK 65536

uint64_t xrd_fnv1a64(uint64_t h, const void *data, size_t len);

/* 0 and key "<size>-<16 hex>"; -1 if path is not a readable regular file
 * (after following symlinks) or key does not fit. */
int xrd_filestate_key(const char *path, char *key, size_t keylen);

/* ".../xrdome/files" per Global Constraints; -1 if neither variable is usable. */
int xrd_filestate_dir(char *buf, size_t len);

#define XRD_FILESTATE_VERSION 1
#define XRD_FILESTATE_MAX_BYTES 65536

typedef enum { XRD_FS_OK, XRD_FS_NONE, XRD_FS_UNSUPPORTED, XRD_FS_BAD } xrd_fs_status;

/* Parses record text. OK fills *out; UNSUPPORTED (version > 1, or proj other
 * than hequirect or flat) and BAD leave *out with set == 0. err gets "line N: ..." for BAD. */
xrd_fs_status xrd_filestate_parse(const char *text, xrd_filestate *out, char *err, size_t errlen);

/* Writes the record text (spec 5.3). Returns length, or -1 if it does not fit. */
int xrd_filestate_format(const xrd_filestate *st, const char *path_label, const char *date,
                         char *buf, size_t len);

/* "<dir>/<key>.ini"; -1 if it does not fit. */
int xrd_filestate_record_path(const char *dir, const char *key, char *buf, size_t len);

/* NONE when the file does not exist (ENOENT or ENOTDIR); BAD (with err) for
 * any other read error or a file over 65536 bytes. */
xrd_fs_status xrd_filestate_load(const char *record_path, xrd_filestate *out,
                                 char *err, size_t errlen);

/* Creates dir (and parents, 0700), writes a temp file in dir, renames it to
 * "<key>.ini". video_path is made absolute for the label. 0, or -1 with err. */
int xrd_filestate_save(const char *dir, const char *key, const xrd_filestate *st,
                       const char *video_path, char *err, size_t errlen);

/* 1 deleted, 0 no such record, -1 error. */
int xrd_filestate_delete(const char *dir, const char *key);

/* ---- merging a record with flags and runtime changes ---------------------- */

#define XRD_FS_TOL_ZOOM 0.001f
#define XRD_FS_TOL_FOV 0.01f
#define XRD_FS_TOL_CONVERGENCE 0.001f

/* Copies each field that is set in rec and whose bit is NOT in explicit_flags
 * into *s. Returns the bits copied. */
unsigned xrd_filestate_apply(const xrd_filestate *rec, unsigned explicit_flags,
                             xrd_file_settings *s);
/* Spec 5.5. A field is set in *out when it is set in loaded (may be NULL), or
 * its bit is in explicit_flags, or end differs from start beyond the
 * tolerance. Values come from end. */
void xrd_filestate_collect(const xrd_filestate *loaded, unsigned explicit_flags,
                           const xrd_file_settings *start, const xrd_file_settings *end,
                           xrd_filestate *out);
/* Same set bits, and set fields equal within the tolerances. */
bool xrd_filestate_equal(const xrd_filestate *a, const xrd_filestate *b);
/* "zoom=0.75 layout=sbs-rl swap=yes content-fov=120x90 convergence=1.5 proj=flat
 * flat-zoom=1.65 packing=half zoom-mode=magnify" for the fields in
 * (st->set & mask), in that order, space separated. Returns length, 0 for
 * none, -1 if it does not fit. Formats: zoom and flat-zoom %.2f, fov %gx%g,
 * convergence %.1f. */
int xrd_filestate_describe(const xrd_filestate *st, unsigned mask, char *buf, size_t len);

#endif
