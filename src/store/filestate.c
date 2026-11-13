#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "store/filestate.h"

#include "values.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

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

/* ---- record parse and format ------------------------------------------ */

typedef enum { KEY_ZOOM, KEY_LAYOUT, KEY_SWAP, KEY_CONTENT_FOV, KEY_CONVERGENCE } setting_key;

/* One table of key names, shared by parse and format. */
static const struct { const char *name; unsigned bit; } k_settings[] = {
    [KEY_ZOOM] = { "zoom", XRD_SET_ZOOM },
    [KEY_LAYOUT] = { "layout", XRD_SET_LAYOUT },
    [KEY_SWAP] = { "swap", XRD_SET_SWAP },
    [KEY_CONTENT_FOV] = { "content_fov", XRD_SET_CONTENT_FOV },
    [KEY_CONVERGENCE] = { "convergence", XRD_SET_CONVERGENCE },
};
#define N_SETTINGS (sizeof k_settings / sizeof k_settings[0])

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

/* Applies one settings key to st; false if the value is not acceptable. */
static bool apply_setting(setting_key k, const char *val, xrd_filestate *st)
{
    switch (k) {
    case KEY_ZOOM:
        if (xrd_value_ranged(val, XRD_ZOOM_MIN, XRD_ZOOM_MAX, &st->v.zoom) != XRD_VALUE_OK)
            return false;
        break;
    case KEY_LAYOUT:
        if (!xrd_value_layout(val, &st->v.layout))
            return false;
        break;
    case KEY_SWAP:
        if (!xrd_value_yesno(val, &st->v.swap))
            return false;
        break;
    case KEY_CONTENT_FOV:
        if (xrd_value_content_fov(val, &st->v.content_hfov, &st->v.content_vfov) != XRD_VALUE_OK)
            return false;
        break;
    case KEY_CONVERGENCE:
        if (xrd_value_ranged(val, -XRD_CONVERGENCE_MAX, XRD_CONVERGENCE_MAX,
                             &st->v.convergence) != XRD_VALUE_OK)
            return false;
        break;
    }
    st->set |= k_settings[k].bit;
    return true;
}

/* Trims blanks, tabs and CR from both ends of [*b, *e). */
static void trim(const char **b, const char **e)
{
    while (*b < *e && isspace((unsigned char)**b))
        (*b)++;
    while (*e > *b && isspace((unsigned char)(*e)[-1]))
        (*e)--;
}

static bool span_is(const char *b, const char *e, const char *word)
{
    size_t n = strlen(word);
    return (size_t)(e - b) == n && !memcmp(b, word, n);
}

xrd_fs_status xrd_filestate_parse(const char *text, xrd_filestate *out, char *err, size_t errlen)
{
    xrd_filestate st;
    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof *out);

    bool have_version = false, unsupported = false;
    int bad_line = 0;
    char bad_msg[96] = "";
    int lineno = 0;

    for (const char *p = text; *p;) {
        const char *nl = strchr(p, '\n');
        const char *b = p, *e = nl ? nl : p + strlen(p);
        p = nl ? nl + 1 : e;
        lineno++;
        trim(&b, &e);
        if (b == e || *b == '#')
            continue;

        const char *eq = memchr(b, '=', (size_t)(e - b));
        if (!eq) {
            if (!bad_line) {
                bad_line = lineno;
                snprintf(bad_msg, sizeof bad_msg, "not a 'key = value' line");
            }
            continue;
        }
        const char *kb = b, *ke = eq, *vb = eq + 1, *ve = e;
        trim(&kb, &ke);
        trim(&vb, &ve);

        char val[64];
        bool val_fits = (size_t)(ve - vb) < sizeof val;
        if (val_fits) {
            memcpy(val, vb, (size_t)(ve - vb));
            val[ve - vb] = '\0';
        }

        if (kb == ke) {
            if (!bad_line) {
                bad_line = lineno;
                snprintf(bad_msg, sizeof bad_msg, "missing key before '='");
            }
        } else if (span_is(kb, ke, "version")) {
            char *end;
            long v = 0;
            bool ok = val_fits && isdigit((unsigned char)val[0]);
            if (ok) {
                errno = 0;
                v = strtol(val, &end, 10);
                ok = *end == '\0' && errno != ERANGE && v >= 1;
            }
            if (!ok) {
                if (!bad_line) {
                    bad_line = lineno;
                    snprintf(bad_msg, sizeof bad_msg, "version is not a positive integer");
                }
            } else {
                have_version = true;
                if (v > XRD_FILESTATE_VERSION)
                    unsupported = true;
            }
        } else if (span_is(kb, ke, "proj")) {
            if (!span_is(vb, ve, "hequirect"))
                unsupported = true;
        } else if (span_is(kb, ke, "path") || span_is(kb, ke, "saved")) {
            /* labels for people; never used */
        } else {
            for (size_t k = 0; k < N_SETTINGS; k++) {
                if (!span_is(kb, ke, k_settings[k].name))
                    continue;
                if (!(val_fits && apply_setting((setting_key)k, val, &st)) && !bad_line) {
                    bad_line = lineno;
                    snprintf(bad_msg, sizeof bad_msg, "bad value for %s", k_settings[k].name);
                }
                break;
            }
            /* unknown keys are ignored */
        }
    }

    if (unsupported)
        return XRD_FS_UNSUPPORTED;
    if (bad_line) {
        set_err(err, errlen, "line %d: %s", bad_line, bad_msg);
        return XRD_FS_BAD;
    }
    if (!have_version) {
        set_err(err, errlen, "no version line");
        return XRD_FS_BAD;
    }
    *out = st;
    return XRD_FS_OK;
}

/* Bounded appender: pos keeps counting past the end so overflow is detectable. */
typedef struct { char *buf; size_t len, pos; } sink;

static void put(sink *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(sink *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *at = s->pos < s->len ? s->buf + s->pos : NULL;
    int n = vsnprintf(at, at ? s->len - s->pos : 0, fmt, ap);
    va_end(ap);
    s->pos += n > 0 ? (size_t)n : 0;
}

int xrd_filestate_format(const xrd_filestate *st, const char *path_label, const char *date,
                         char *buf, size_t len)
{
    sink s = { buf, len, 0 };
    put(&s, "# xrdome per-file settings\nversion = %d\npath = ", XRD_FILESTATE_VERSION);
    for (const unsigned char *c = (const unsigned char *)path_label; *c; c++)
        put(&s, "%c", (*c < 0x20 || *c == 0x7f) ? '?' : *c);
    put(&s, "\nsaved = %s\nproj = hequirect\n", date);

    if (st->set & k_settings[KEY_ZOOM].bit)
        put(&s, "%s = %.4f\n", k_settings[KEY_ZOOM].name, (double)st->v.zoom);
    if (st->set & k_settings[KEY_LAYOUT].bit)
        put(&s, "%s = %s\n", k_settings[KEY_LAYOUT].name,
            xrd_value_layout_name(st->v.layout) ? xrd_value_layout_name(st->v.layout) : "sbs-lr");
    if (st->set & k_settings[KEY_SWAP].bit)
        put(&s, "%s = %s\n", k_settings[KEY_SWAP].name, st->v.swap ? "yes" : "no");
    if (st->set & k_settings[KEY_CONTENT_FOV].bit)
        put(&s, "%s = %gx%g\n", k_settings[KEY_CONTENT_FOV].name, (double)st->v.content_hfov,
            (double)st->v.content_vfov);
    if (st->set & k_settings[KEY_CONVERGENCE].bit)
        put(&s, "%s = %.3f\n", k_settings[KEY_CONVERGENCE].name, (double)st->v.convergence);

    if (s.pos >= len || s.pos > (size_t)INT_MAX)
        return -1;
    return (int)s.pos;
}

/* ---- files -------------------------------------------------------------- */

int xrd_filestate_record_path(const char *dir, const char *key, char *buf, size_t len)
{
    int w = snprintf(buf, len, "%s/%s.ini", dir, key);
    return (w < 0 || (size_t)w >= len) ? -1 : 0;
}

xrd_fs_status xrd_filestate_load(const char *record_path, xrd_filestate *out,
                                 char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen(record_path, "rb");
    if (!f) {
        if (errno == ENOENT || errno == ENOTDIR)
            return XRD_FS_NONE;
        set_err(err, errlen, "cannot open %s: %s", record_path, strerror(errno));
        return XRD_FS_BAD;
    }
    static char text[XRD_FILESTATE_MAX_BYTES + 2];
    size_t n = fread(text, 1, XRD_FILESTATE_MAX_BYTES + 1, f);
    bool failed = ferror(f);
    int saved_errno = errno;
    fclose(f);
    if (failed) {
        set_err(err, errlen, "cannot read %s: %s", record_path, strerror(saved_errno));
        return XRD_FS_BAD;
    }
    if (n > XRD_FILESTATE_MAX_BYTES) {
        set_err(err, errlen, "%s is larger than %d bytes", record_path, XRD_FILESTATE_MAX_BYTES);
        return XRD_FS_BAD;
    }
    text[n] = '\0';
    if (strlen(text) != n) {
        set_err(err, errlen, "%s contains a NUL byte", record_path);
        return XRD_FS_BAD;
    }
    return xrd_filestate_parse(text, out, err, errlen);
}

/* mkdir -p, mode 0700. */
static int make_dirs(const char *dir, char *err, size_t errlen)
{
    char tmp[PATH_MAX];
    size_t n = strlen(dir);
    if (n == 0 || n >= sizeof tmp) {
        set_err(err, errlen, "state directory path is empty or too long");
        return -1;
    }
    memcpy(tmp, dir, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (tmp[i] != '/' && tmp[i] != '\0')
            continue;
        char saved = tmp[i];
        tmp[i] = '\0';
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
            set_err(err, errlen, "cannot create %s: %s", tmp, strerror(errno));
            return -1;
        }
        tmp[i] = saved;
    }
    struct stat sb;
    if (stat(dir, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        set_err(err, errlen, "%s is not a directory", dir);
        return -1;
    }
    return 0;
}

int xrd_filestate_save(const char *dir, const char *key, const xrd_filestate *st,
                       const char *video_path, char *err, size_t errlen)
{
    char label[PATH_MAX * 2], rec[PATH_MAX], tmp[PATH_MAX], date[16], text[PATH_MAX * 2 + 512];

    if (strchr(key, '/')) {
        set_err(err, errlen, "invalid record key '%s'", key);
        return -1;
    }
    if (video_path[0] == '/') {
        snprintf(label, sizeof label, "%s", video_path);
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd))
            cwd[0] = '\0';
        snprintf(label, sizeof label, "%s/%s", cwd, video_path);
    }
    time_t now = time(NULL);
    struct tm tm;
    if (!localtime_r(&now, &tm) || strftime(date, sizeof date, "%Y-%m-%d", &tm) == 0)
        snprintf(date, sizeof date, "unknown");

    int n = xrd_filestate_format(st, label, date, text, sizeof text);
    if (n < 0 || xrd_filestate_record_path(dir, key, rec, sizeof rec) != 0) {
        set_err(err, errlen, "record for %s does not fit its buffer", key);
        return -1;
    }
    if (make_dirs(dir, err, errlen) != 0)
        return -1;

    int w = snprintf(tmp, sizeof tmp, "%s/.tmp-XXXXXX", dir);
    if (w < 0 || (size_t)w >= sizeof tmp) {
        set_err(err, errlen, "temporary path in %s is too long", dir);
        return -1;
    }
    int fd = mkstemp(tmp);
    if (fd < 0) {
        set_err(err, errlen, "cannot create a temporary file in %s: %s", dir, strerror(errno));
        return -1;
    }
    const char *p = text;
    size_t left = (size_t)n;
    while (left > 0) {
        ssize_t k = write(fd, p, left);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0) {
            if (k == 0)
                set_err(err, errlen, "cannot write %s: short write", tmp);
            else
                set_err(err, errlen, "cannot write %s: %s", tmp, strerror(errno));
            close(fd);
            unlink(tmp);
            return -1;
        }
        p += k;
        left -= (size_t)k;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        set_err(err, errlen, "cannot write %s: %s", tmp, strerror(errno));
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, rec) != 0) {
        set_err(err, errlen, "cannot rename %s to %s: %s", tmp, rec, strerror(errno));
        unlink(tmp);
        return -1;
    }
    return 0;
}

int xrd_filestate_delete(const char *dir, const char *key)
{
    char rec[PATH_MAX];
    if (xrd_filestate_record_path(dir, key, rec, sizeof rec) != 0)
        return -1;
    if (unlink(rec) == 0)
        return 1;
    return (errno == ENOENT || errno == ENOTDIR) ? 0 : -1;
}

/* ---- apply, collect, equal, describe ------------------------------------ */

static void copy_field(unsigned bit, const xrd_file_settings *from, xrd_file_settings *to)
{
    switch (bit) {
    case XRD_SET_ZOOM: to->zoom = from->zoom; break;
    case XRD_SET_LAYOUT: to->layout = from->layout; break;
    case XRD_SET_SWAP: to->swap = from->swap; break;
    case XRD_SET_CONTENT_FOV:
        to->content_hfov = from->content_hfov;
        to->content_vfov = from->content_vfov;
        break;
    case XRD_SET_CONVERGENCE: to->convergence = from->convergence; break;
    }
}

/* True when field `bit` differs between a and b beyond its tolerance. */
static bool field_differs(unsigned bit, const xrd_file_settings *a, const xrd_file_settings *b)
{
    switch (bit) {
    case XRD_SET_ZOOM: return fabsf(a->zoom - b->zoom) > XRD_FS_TOL_ZOOM;
    case XRD_SET_LAYOUT: return a->layout != b->layout;
    case XRD_SET_SWAP: return a->swap != b->swap;
    case XRD_SET_CONTENT_FOV:
        return fabsf(a->content_hfov - b->content_hfov) > XRD_FS_TOL_FOV
            || fabsf(a->content_vfov - b->content_vfov) > XRD_FS_TOL_FOV;
    case XRD_SET_CONVERGENCE:
        return fabsf(a->convergence - b->convergence) > XRD_FS_TOL_CONVERGENCE;
    }
    return false;
}

unsigned xrd_filestate_apply(const xrd_filestate *rec, unsigned explicit_flags,
                             xrd_file_settings *s)
{
    unsigned applied = 0;
    for (size_t k = 0; k < N_SETTINGS; k++) {
        unsigned bit = k_settings[k].bit;
        if ((rec->set & bit) && !(explicit_flags & bit)) {
            copy_field(bit, &rec->v, s);
            applied |= bit;
        }
    }
    return applied;
}

void xrd_filestate_collect(const xrd_filestate *loaded, unsigned explicit_flags,
                           const xrd_file_settings *start, const xrd_file_settings *end,
                           xrd_filestate *out)
{
    memset(out, 0, sizeof *out);
    for (size_t k = 0; k < N_SETTINGS; k++) {
        unsigned bit = k_settings[k].bit;
        if ((loaded && (loaded->set & bit)) || (explicit_flags & bit)
            || field_differs(bit, start, end)) {
            out->set |= bit;
            copy_field(bit, end, &out->v);
        }
    }
}

bool xrd_filestate_equal(const xrd_filestate *a, const xrd_filestate *b)
{
    if (a->set != b->set)
        return false;
    for (size_t k = 0; k < N_SETTINGS; k++) {
        unsigned bit = k_settings[k].bit;
        if ((a->set & bit) && field_differs(bit, &a->v, &b->v))
            return false;
    }
    return true;
}

int xrd_filestate_describe(const xrd_filestate *st, unsigned mask, char *buf, size_t len)
{
    sink s = { buf, len, 0 };
    unsigned want = st->set & mask;
    const char *sep = "";

    if (len > 0)
        buf[0] = '\0';
    if (want & XRD_SET_ZOOM) {
        put(&s, "%szoom=%.2f", sep, (double)st->v.zoom);
        sep = " ";
    }
    if (want & XRD_SET_LAYOUT) {
        const char *name = xrd_value_layout_name(st->v.layout);
        put(&s, "%slayout=%s", sep, name ? name : "?");
        sep = " ";
    }
    if (want & XRD_SET_SWAP) {
        put(&s, "%sswap=%s", sep, st->v.swap ? "yes" : "no");
        sep = " ";
    }
    if (want & XRD_SET_CONTENT_FOV) {
        put(&s, "%scontent-fov=%gx%g", sep, (double)st->v.content_hfov,
            (double)st->v.content_vfov);
        sep = " ";
    }
    if (want & XRD_SET_CONVERGENCE)
        put(&s, "%sconvergence=%.1f", sep, (double)st->v.convergence);

    if (s.pos >= len || s.pos > (size_t)INT_MAX) {
        if (len > 0)
            buf[0] = '\0';
        return -1;
    }
    return (int)s.pos;
}
