#define _POSIX_C_SOURCE 200809L

#include "render/depthmap.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errlen, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void set_err(char *err, size_t errlen, const char *fmt, ...)
{
    if (!err || errlen == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

/* ---- sha256 (FIPS 180-4) ------------------------------------------------- */

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    unsigned char buf[64];
    size_t n;
} sha256_ctx;

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_init(sha256_ctx *c)
{
    static const uint32_t h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->h, h0, sizeof h0);
    c->bytes = 0;
    c->n = 0;
}

static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    uint32_t e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25)) + ((e & f) ^ (~e & g)) +
                      K256[i] + w[i];
        uint32_t t2 = (ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = cc;
        cc = b;
        b = a;
        a = t1 + t2;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
    c->h[5] += f;
    c->h[6] += g;
    c->h[7] += h;
}

static void sha256_update(sha256_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = data;
    c->bytes += len;
    if (c->n > 0) {
        size_t take = 64 - c->n < len ? 64 - c->n : len;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        len -= take;
        if (c->n < 64)
            return;
        sha256_block(c, c->buf);
        c->n = 0;
    }
    for (; len >= 64; p += 64, len -= 64)
        sha256_block(c, p);
    memcpy(c->buf, p, len);
    c->n = len;
}

static void sha256_hex_final(sha256_ctx *c, char hex[XRD_DEPTH_KEY_LEN + 1])
{
    uint64_t bits = c->bytes * 8;
    unsigned char pad[72] = {0x80};
    size_t padlen = (c->n < 56 ? 56 : 120) - c->n;
    for (int i = 0; i < 8; i++)
        pad[padlen + (size_t)i] = (unsigned char)(bits >> (56 - 8 * i));
    sha256_update(c, pad, padlen + 8);
    for (int i = 0; i < 8; i++)
        snprintf(hex + 8 * i, 9, "%08x", (unsigned)c->h[i]);
}

void xrd_sha256_hex(const void *data, size_t len, char hex[XRD_DEPTH_KEY_LEN + 1])
{
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_hex_final(&c, hex);
}

int xrd_depth_key(const char *path, char key[XRD_DEPTH_KEY_LEN + 1])
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    sha256_ctx c;
    sha256_init(&c);
    unsigned char buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        sha256_update(&c, buf, n);
    bool failed = ferror(f);
    int saved = errno;
    fclose(f);
    if (failed) {
        errno = saved;
        return -1;
    }
    sha256_hex_final(&c, key);
    return 0;
}

/* ---- the cache directory -------------------------------------------------- */

int xrd_depth_cache_dir(char *buf, size_t len)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    int w;
    if (xdg && xdg[0] == '/')
        w = snprintf(buf, len, "%s/xrdome-tools/depth", xdg);
    else if (home && home[0])
        w = snprintf(buf, len, "%s/.cache/xrdome-tools/depth", home);
    else
        return -1;
    return (w < 0 || (size_t)w >= len) ? -1 : 0;
}

int xrd_depth_cache_path(const char *dir, const char *key, char *buf, size_t len)
{
    int w = snprintf(buf, len, "%s/%s%s", dir, key, XRD_DEPTH_SUFFIX);
    return (w < 0 || (size_t)w >= len) ? -1 : 0;
}

/* ---- PGM ---------------------------------------------------------------- */

static bool pgm_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* Skips whitespace (at least one byte), then reads a decimal number of at
 * most 9 digits. */
static bool pgm_number(const unsigned char *d, size_t len, size_t *at, long *out)
{
    size_t i = *at;
    if (i >= len || !pgm_space(d[i]))
        return false;
    while (i < len && pgm_space(d[i]))
        i++;
    long v = 0;
    size_t digits = 0;
    while (i < len && d[i] >= '0' && d[i] <= '9' && digits < 10) {
        v = v * 10 + (d[i] - '0');
        i++;
        digits++;
    }
    if (digits == 0 || digits > 9)
        return false;
    *at = i;
    *out = v;
    return true;
}

xrd_depth_status xrd_depthmap_parse(const unsigned char *data, size_t len, xrd_depthmap *out,
                                    char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    long w = 0, h = 0, maxval = 0;
    size_t at = 2;
    if (len < 2 || data[0] != 'P' || data[1] != '5') {
        set_err(err, errlen, "not a binary PGM (no P5)");
        return XRD_DEPTH_BAD;
    }
    if (!pgm_number(data, len, &at, &w) || !pgm_number(data, len, &at, &h) ||
        !pgm_number(data, len, &at, &maxval) || at >= len || !pgm_space(data[at])) {
        set_err(err, errlen, "bad PGM header");
        return XRD_DEPTH_BAD;
    }
    at++; /* the one whitespace byte before the samples */
    if (w < 1 || h < 1 || w > XRD_DEPTH_MAX_SIDE || h > XRD_DEPTH_MAX_SIDE) {
        set_err(err, errlen, "PGM size %ldx%ld is outside 1 to %d", w, h, XRD_DEPTH_MAX_SIDE);
        return XRD_DEPTH_BAD;
    }
    if (maxval != 65535) {
        set_err(err, errlen, "PGM maxval %ld, expected 65535", maxval);
        return XRD_DEPTH_BAD;
    }
    size_t n = (size_t)w * (size_t)h;
    if (len - at != 2 * n) {
        set_err(err, errlen, "%zu bytes of samples for %ldx%ld (need %zu)", len - at, w, h, 2 * n);
        return XRD_DEPTH_BAD;
    }
    uint16_t *px = malloc(n * sizeof *px);
    if (!px) {
        set_err(err, errlen, "out of memory for %ldx%ld", w, h);
        return XRD_DEPTH_BAD;
    }
    const unsigned char *s = data + at;
    for (size_t i = 0; i < n; i++)
        px[i] = (uint16_t)(s[2 * i] << 8 | s[2 * i + 1]);
    out->w = (int)w;
    out->h = (int)h;
    out->px = px;
    return XRD_DEPTH_OK;
}

xrd_depth_status xrd_depthmap_read(const char *path, xrd_depthmap *out, char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT || errno == ENOTDIR)
            return XRD_DEPTH_NONE;
        set_err(err, errlen, "cannot open: %s", strerror(errno));
        return XRD_DEPTH_BAD;
    }
    /* The header is at most about 30 bytes; anything past the largest valid
     * file is refused unread. */
    size_t cap = 64 + 2 * (size_t)XRD_DEPTH_MAX_SIDE * XRD_DEPTH_MAX_SIDE, len = 0, size = 1 << 16;
    unsigned char *data = malloc(size);
    while (data) {
        if (len == size) {
            if (size > cap)
                break;
            unsigned char *more = realloc(data, size * 2);
            if (!more) {
                free(data);
                data = NULL;
                break;
            }
            data = more;
            size *= 2;
        }
        size_t k = fread(data + len, 1, size - len, f);
        len += k;
        if (k == 0)
            break;
    }
    bool failed = ferror(f);
    int saved = errno;
    fclose(f);
    if (!data) {
        set_err(err, errlen, "out of memory");
        return XRD_DEPTH_BAD;
    }
    if (failed) {
        free(data);
        set_err(err, errlen, "cannot read: %s", strerror(saved));
        return XRD_DEPTH_BAD;
    }
    if (len > cap) {
        free(data);
        set_err(err, errlen, "larger than any depth map of at most %dx%d", XRD_DEPTH_MAX_SIDE,
                XRD_DEPTH_MAX_SIDE);
        return XRD_DEPTH_BAD;
    }
    xrd_depth_status st = xrd_depthmap_parse(data, len, out, err, errlen);
    free(data);
    return st;
}

void xrd_depthmap_free(xrd_depthmap *m)
{
    free(m->px);
    m->px = NULL;
    m->w = m->h = 0;
}

/* ---- EXIF orientation ------------------------------------------------------ */

static unsigned rd16(const unsigned char *p, bool be)
{
    return be ? (unsigned)(p[0] << 8 | p[1]) : (unsigned)(p[1] << 8 | p[0]);
}

static uint32_t rd32(const unsigned char *p, bool be)
{
    return be ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]
              : (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

/* The Orientation tag of IFD0 in a TIFF block, or 0. */
static int tiff_orientation(const unsigned char *t, size_t len)
{
    if (len < 8)
        return 0;
    bool be;
    if (t[0] == 'M' && t[1] == 'M')
        be = true;
    else if (t[0] == 'I' && t[1] == 'I')
        be = false;
    else
        return 0;
    if (rd16(t + 2, be) != 42)
        return 0;
    uint32_t ifd = rd32(t + 4, be);
    if (ifd > len - 2)
        return 0;
    unsigned n = rd16(t + ifd, be);
    for (unsigned i = 0; i < n; i++) {
        size_t e = (size_t)ifd + 2 + 12 * (size_t)i;
        if (e + 12 > len)
            return 0;
        if (rd16(t + e, be) != 0x0112)
            continue;
        /* SHORT, count 1: the value sits in the first two bytes of the field. */
        if (rd16(t + e + 2, be) != 3 || rd32(t + e + 4, be) != 1)
            return 0;
        unsigned v = rd16(t + e + 8, be);
        return v >= 1 && v <= 8 ? (int)v : 0;
    }
    return 0;
}

int xrd_exif_orientation_jpeg(const unsigned char *d, size_t len)
{
    if (len < 4 || d[0] != 0xff || d[1] != 0xd8)
        return 0;
    size_t at = 2;
    while (at + 4 <= len) {
        if (d[at] != 0xff)
            return 0;
        unsigned char marker = d[at + 1];
        if (marker == 0xff) { /* fill byte */
            at++;
            continue;
        }
        if (marker == 0xda || marker == 0xd9) /* start of scan, end of image: no more headers */
            return 0;
        size_t seg = (size_t)(d[at + 2] << 8 | d[at + 3]);
        if (seg < 2 || at + 2 + seg > len)
            return 0;
        const unsigned char *body = d + at + 4;
        size_t blen = seg - 2;
        if (marker == 0xe1 && blen >= 6 && memcmp(body, "Exif\0\0", 6) == 0)
            return tiff_orientation(body + 6, blen - 6);
        at += 2 + seg;
    }
    return 0;
}

int xrd_exif_orientation(const char *path)
{
    /* The APP1 segment is at most 64 KiB and comes before the image data;
     * a few other segments may precede it. */
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    static unsigned char buf[256 * 1024];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return xrd_exif_orientation_jpeg(buf, n);
}

bool xrd_exif_mirrored(int orientation)
{
    return orientation == 2 || orientation == 4 || orientation == 5 || orientation == 7;
}

bool xrd_depth_orientation_ok(int orientation, int mpv_major, int mpv_minor)
{
    if (!xrd_exif_mirrored(orientation))
        return true;
    return mpv_major > 0 || (mpv_major == 0 && mpv_minor >= 42);
}

bool xrd_mpv_version_parse(const char *s, int *major, int *minor)
{
    *major = *minor = 0;
    if (!s || strncmp(s, "mpv ", 4) != 0)
        return false;
    s += 4;
    if (*s == 'v')
        s++;
    int a, b;
    char dot;
    if (sscanf(s, "%d%c%d", &a, &dot, &b) != 3 || dot != '.' || a < 0 || b < 0)
        return false;
    *major = a;
    *minor = b;
    return true;
}

bool xrd_depth_aspect_matches(int w, int h, int pw, int ph)
{
    if (w < 1 || h < 1 || pw < 1 || ph < 1)
        return false;
    double a = (double)w / h, pa = (double)pw / ph;
    /* A side rounded by up to one pixel moves the aspect by up to a/w or a/h. */
    return fabs(a - pa) <= pa * (1.0 / w + 1.0 / h) + 1e-9;
}
