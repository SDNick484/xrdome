#ifndef XRD_RENDER_DEPTHMAP_H
#define XRD_RENDER_DEPTHMAP_H

/* The depth cache of spec 5 (docs/superpowers/specs/2026-11-19-depth-3d-design.md),
 * read side: the key, the cache path and the PGM reader. Must match
 * tools/xrdome_depth/cache.py, which writes it. Pure file code: no GL. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XRD_DEPTH_SUFFIX "-dav2s-v1.pgm"
#define XRD_DEPTH_KEY_LEN 64      /* hex digits of a sha256 */
#define XRD_DEPTH_MAX_SIDE 8192   /* larger PGMs are refused (the helper caps at 2048) */

/* sha256 of len bytes at data, as 64 lowercase hex digits and a NUL. */
void xrd_sha256_hex(const void *data, size_t len, char hex[XRD_DEPTH_KEY_LEN + 1]);

/* The cache key: sha256 of the file's bytes, as cache.cache_key. 0, or -1
 * (errno set) when the file cannot be read. */
int xrd_depth_key(const char *path, char key[XRD_DEPTH_KEY_LEN + 1]);

/* "$XDG_CACHE_HOME/xrdome-tools/depth", or "$HOME/.cache/xrdome-tools/depth"
 * when XDG_CACHE_HOME is unset, empty or relative (xrdome_tools.config.paths).
 * -1 when neither is usable or the path does not fit. */
int xrd_depth_cache_dir(char *buf, size_t len);

/* "<dir>/<key>-dav2s-v1.pgm"; -1 if it does not fit. */
int xrd_depth_cache_path(const char *dir, const char *key, char *buf, size_t len);

/* A depth map: w x h samples, row 0 the top, 0 far and 65535 near. */
typedef struct {
    int w, h;
    uint16_t *px;
} xrd_depthmap;

typedef enum { XRD_DEPTH_OK, XRD_DEPTH_NONE, XRD_DEPTH_BAD } xrd_depth_status;

/* Parses a cache file's bytes: "P5", whitespace, width, whitespace, height,
 * whitespace, "65535", one whitespace byte, then exactly w * h big-endian
 * 16-bit samples. Width and height are 1 to XRD_DEPTH_MAX_SIDE. OK fills *out
 * (free with xrd_depthmap_free); BAD sets err and leaves *out empty. */
xrd_depth_status xrd_depthmap_parse(const unsigned char *data, size_t len, xrd_depthmap *out,
                                    char *err, size_t errlen);
/* Reads and parses path: NONE when it does not exist (ENOENT or ENOTDIR),
 * BAD with err for any other failure. */
xrd_depth_status xrd_depthmap_read(const char *path, xrd_depthmap *out, char *err, size_t errlen);
void xrd_depthmap_free(xrd_depthmap *m);

/* The EXIF Orientation tag (1 to 8) of a JPEG's APP1 Exif segment; 0 when the
 * file is not a JPEG, has no such tag, or cannot be read. */
int xrd_exif_orientation_jpeg(const unsigned char *data, size_t len);
int xrd_exif_orientation(const char *path);

/* True for the Orientation tags that mirror (2, 4, 5 and 7). */
bool xrd_exif_mirrored(int orientation);

/* Whether the depth (in the EXIF-transposed orientation of spec 5) lines up
 * with the picture mpv mpv_major.mpv_minor shows for a photo with this
 * Orientation tag. mpv 0.37 turns the mirrored tags without mirroring them;
 * 0.42 is the first version seen to mirror them (docs/STATUS.md, Photos in
 * 2D); the versions between are untested and count as not mirroring. */
bool xrd_depth_orientation_ok(int orientation, int mpv_major, int mpv_minor);

/* "mpv v0.42.0", "mpv 0.37.0", "mpv v0.40.0-123-gabcdef": major and minor.
 * false (both 0) when s does not hold "<major>.<minor>" after "mpv ". */
bool xrd_mpv_version_parse(const char *s, int *major, int *minor);

/* Whether a w x h depth map fits a picture shown pw x ph: the same aspect
 * within the rounding of the helper's resize (one pixel on either side). */
bool xrd_depth_aspect_matches(int w, int h, int pw, int ph);

#endif
