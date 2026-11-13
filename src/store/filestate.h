#ifndef XRD_FILESTATE_H
#define XRD_FILESTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "render/projection.h"

enum { XRD_SET_ZOOM = 1u << 0, XRD_SET_LAYOUT = 1u << 1, XRD_SET_SWAP = 1u << 2,
       XRD_SET_CONTENT_FOV = 1u << 3, XRD_SET_CONVERGENCE = 1u << 4 };

typedef struct {
    float zoom;
    xrd_layout layout;
    bool swap;
    float content_hfov, content_vfov;
    float convergence;
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

#endif
