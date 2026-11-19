#ifndef XRD_APP_IMAGEDIR_H
#define XRD_APP_IMAGEDIR_H

/* The photos of a directory argument, in name order (README, Photos). Pure
 * file-system code: no SDL, GL or mpv. */

#include <stdbool.h>
#include <stddef.h>

#define XRD_IMAGEDIR_PATH_MAX 4096

typedef struct {
    char (*path)[XRD_IMAGEDIR_PATH_MAX]; /* "<dir>/<name>", count of them */
    size_t count;
} xrd_imagedir;

/* True when path names a directory (after following symlinks). */
bool xrd_imagedir_is_dir(const char *path);

/* Lists the regular files of dir (after following symlinks) whose names pass
 * xrd_detect_image_name, skipping names that start with '.', not descending,
 * sorted by the bytes of the name (so IMG_10 comes before IMG_2). Returns 0,
 * with count >= 1; -1 with err set when dir cannot be read, holds no image, or
 * a path does not fit. */
int xrd_imagedir_open(const char *dir, xrd_imagedir *out, char *err, size_t errlen);
void xrd_imagedir_free(xrd_imagedir *d);

/* The index delta photos from cur (+1 PageDown, -1 PageUp), or -1 when that
 * is before the first or after the last photo: the list does not wrap. */
long xrd_imagedir_step(const xrd_imagedir *d, size_t cur, int delta);

#endif
