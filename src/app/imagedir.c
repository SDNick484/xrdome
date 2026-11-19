#define _POSIX_C_SOURCE 200809L

#include "app/imagedir.h"

#include "detect/detect.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool xrd_imagedir_is_dir(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int by_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

void xrd_imagedir_free(xrd_imagedir *d)
{
    free(d->path);
    d->path = NULL;
    d->count = 0;
}

int xrd_imagedir_open(const char *dir, xrd_imagedir *out, char *err, size_t errlen)
{
    out->path = NULL;
    out->count = 0;
    DIR *dp = opendir(dir);
    if (!dp) {
        snprintf(err, errlen, "cannot read directory %s: %s", dir, strerror(errno));
        return -1;
    }
    size_t cap = 0;
    int rc = 0;
    const char *sep = dir[0] && dir[strlen(dir) - 1] == '/' ? "" : "/";
    for (struct dirent *e; (e = readdir(dp)) != NULL;) {
        if (e->d_name[0] == '.' || !xrd_detect_image_name(e->d_name))
            continue;
        char full[XRD_IMAGEDIR_PATH_MAX];
        if (snprintf(full, sizeof full, "%s%s%s", dir, sep, e->d_name) >= (int)sizeof full) {
            snprintf(err, errlen, "a path in %s is too long: %s", dir, e->d_name);
            rc = -1;
            break;
        }
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        if (out->count == cap) {
            cap = cap ? cap * 2 : 64;
            void *grown = realloc(out->path, cap * sizeof out->path[0]);
            if (!grown) {
                snprintf(err, errlen, "out of memory listing %s", dir);
                rc = -1;
                break;
            }
            out->path = grown;
        }
        memcpy(out->path[out->count++], full, sizeof full);
    }
    closedir(dp);
    if (rc == 0 && out->count == 0) {
        snprintf(err, errlen, "no images (jpg, png, webp, bmp, tif) in %s", dir);
        rc = -1;
    }
    if (rc != 0) {
        xrd_imagedir_free(out);
        return -1;
    }
    qsort(out->path, out->count, sizeof out->path[0], by_name);
    return 0;
}

long xrd_imagedir_step(const xrd_imagedir *d, size_t cur, int delta)
{
    long next = (long)cur + delta;
    if (next < 0 || (size_t)next >= d->count)
        return -1;
    return next;
}
