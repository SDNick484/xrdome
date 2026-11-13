#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "store/filestate.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define FNV1A64_PRIME 0x100000001b3ULL

uint64_t xrd_fnv1a64(uint64_t h, const void *data, size_t len)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV1A64_PRIME;
    }
    return h;
}

/* Hashes len bytes of f starting at off; false on a seek or short read. */
static bool hash_range(FILE *f, off_t off, off_t len, uint64_t *h)
{
    unsigned char buf[8192];
    if (fseeko(f, off, SEEK_SET) != 0)
        return false;
    while (len > 0) {
        size_t want = len < (off_t)sizeof buf ? (size_t)len : sizeof buf;
        if (fread(buf, 1, want, f) != want)
            return false;
        *h = xrd_fnv1a64(*h, buf, want);
        len -= (off_t)want;
    }
    return true;
}

int xrd_filestate_key(const char *path, char *key, size_t keylen)
{
    struct stat st;
    if (!path || !key || stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;

    off_t size = st.st_size;
    off_t n = size < XRD_FILESTATE_CHUNK ? size : XRD_FILESTATE_CHUNK;
    uint64_t h = XRD_FNV1A64_INIT;

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    bool ok = hash_range(f, 0, n, &h) && hash_range(f, size - n, n, &h);
    fclose(f);
    if (!ok)
        return -1;

    char tmp[XRD_FILESTATE_KEY_MAX];
    int w = snprintf(tmp, sizeof tmp, "%jd-%016" PRIx64, (intmax_t)size, h);
    if (w < 0 || (size_t)w >= sizeof tmp || (size_t)w >= keylen)
        return -1;
    memcpy(key, tmp, (size_t)w + 1);
    return 0;
}

int xrd_filestate_dir(char *buf, size_t len)
{
    const char *xdg = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    int w;
    if (xdg && xdg[0] == '/')
        w = snprintf(buf, len, "%s/xrdome/files", xdg);
    else if (home && home[0])
        w = snprintf(buf, len, "%s/.local/state/xrdome/files", home);
    else
        return -1;
    return (w < 0 || (size_t)w >= len) ? -1 : 0;
}
