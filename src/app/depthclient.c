#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* glibc: posix_spawn_file_actions_addclosefrom_np; also POSIX 2008 */
#endif

#include "app/depthclient.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define SUN_PATH_LEN sizeof(((struct sockaddr_un *)0)->sun_path)
#define LINE_MAX_BYTES (1u << 20) /* a reply longer than this is a broken helper */

/* No SIGPIPE when the helper goes away mid-write: MSG_NOSIGNAL on Linux,
 * SO_NOSIGPIPE on the socket on macOS (set in try_connect). */
#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

/* ---- paths ---------------------------------------------------------------- */

/* base without trailing slashes (but "/" stays), as Python's Path joins it. */
static int join(char *buf, size_t len, const char *base, const char *name)
{
    size_t bl = strlen(base);
    while (bl > 1 && base[bl - 1] == '/')
        bl--;
    int n = snprintf(buf, len, "%.*s%s%s", (int)bl, base, bl == 1 ? "" : "/", name);
    return n < 0 || (size_t)n >= len ? -1 : n;
}

int xrd_depth_socket_path(char *buf, size_t len)
{
    const char *run = getenv("XDG_RUNTIME_DIR");
    int n;
    if (run && run[0] == '/') {
        n = join(buf, len, run, "xrdome-depth.sock");
    } else {
        const char *tmp = getenv("TMPDIR");
        char name[64];
        snprintf(name, sizeof name, "xrdome-depth-%lu.sock", (unsigned long)getuid());
        n = join(buf, len, tmp && tmp[0] == '/' ? tmp : "/tmp", name);
    }
    if (n < 0 || (size_t)n >= SUN_PATH_LEN)
        return -1;
    return 0;
}

int xrd_depth_log_path(const char *socket_path, char *buf, size_t len)
{
    size_t n = strlen(socket_path);
    if (n >= 5 && strcmp(socket_path + n - 5, ".sock") == 0)
        n -= 5;
    int w = snprintf(buf, len, "%.*s.log", (int)n, socket_path);
    return w < 0 || (size_t)w >= len ? -1 : 0;
}

int xrd_depth_abs_path(const char *path, char *buf, size_t len)
{
    char *r = realpath(path, NULL);
    if (!r)
        return -1;
    int n = snprintf(buf, len, "%s", r);
    free(r);
    if (n < 0 || (size_t)n >= len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

/* ---- protocol --------------------------------------------------------------- */

int xrd_depth_request_line(const char *path, bool prefetch, char *buf, size_t len)
{
    static const char head[] = "{\"op\": \"photo\", \"path\": \"";
    size_t n = 0;
#define PUT(s, k)                        \
    do {                                 \
        if (n + (k) >= len)              \
            return -1;                   \
        memcpy(buf + n, (s), (k));       \
        n += (k);                        \
    } while (0)
    PUT(head, sizeof head - 1);
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        char esc[8];
        if (*p == '"' || *p == '\\') {
            esc[0] = '\\';
            esc[1] = (char)*p;
            PUT(esc, 2);
        } else if (*p < 0x20) {
            snprintf(esc, sizeof esc, "\\u%04x", *p);
            PUT(esc, 6);
        } else {
            PUT(p, 1); /* other bytes as they are, UTF-8 or not */
        }
    }
    const char *tail = prefetch ? "\", \"prefetch\": true}\n" : "\", \"prefetch\": false}\n";
    PUT(tail, strlen(tail));
#undef PUT
    buf[n] = '\0';
    return (int)n;
}

int xrd_depth_video_line(const char *shm, char *buf, size_t len)
{
    for (const char *p = shm; *p; p++)
        if (*p == '"' || *p == '\\' || (unsigned char)*p < 0x20)
            return -1; /* ring names are xrdome's own: nothing to escape */
    int n = snprintf(buf, len, "{\"op\": \"video\", \"shm\": \"%s\"}\n", shm);
    return n < 0 || (size_t)n >= len ? -1 : n;
}

typedef struct {
    const char *p, *end;
} cursor;

static void skip_ws(cursor *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n'))
        c->p++;
}

static bool literal(cursor *c, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, word, n) != 0)
        return false;
    c->p += n;
    return true;
}

static int hex4(cursor *c)
{
    if (c->end - c->p < 4)
        return -1;
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char h = *c->p++;
        v <<= 4;
        if (h >= '0' && h <= '9')
            v |= h - '0';
        else if (h >= 'a' && h <= 'f')
            v |= h - 'a' + 10;
        else if (h >= 'A' && h <= 'F')
            v |= h - 'A' + 10;
        else
            return -1;
    }
    return v;
}

/* Appends k bytes to dst[cap] at *n; false when they do not fit (unless
 * truncating, which drops them). dst NULL: skip. */
static bool emit(char *dst, size_t cap, size_t *n, const char *s, size_t k, bool truncate)
{
    if (!dst)
        return true;
    if (*n + k >= cap) {
        if (!truncate)
            return false;
        k = *n + 1 < cap ? cap - 1 - *n : 0;
    }
    memcpy(dst + *n, s, k);
    *n += k;
    return true;
}

/* A JSON string into dst[cap] (NUL terminated). \u escapes become UTF-8; a
 * lone \udc80 to \udcff is the byte 0x80 to 0xff (Python's surrogateescape,
 * for a reply sent with ensure_ascii). */
static bool parse_string(cursor *c, char *dst, size_t cap, bool truncate)
{
    size_t n = 0;
    if (c->p >= c->end || *c->p != '"')
        return false;
    c->p++;
    for (;;) {
        if (c->p >= c->end)
            return false;
        unsigned char ch = (unsigned char)*c->p++;
        if (ch == '"')
            break;
        if (ch < 0x20)
            return false;
        if (ch != '\\') {
            if (!emit(dst, cap, &n, (const char *)&ch, 1, truncate))
                return false;
            continue;
        }
        if (c->p >= c->end)
            return false;
        char e = *c->p++, one;
        switch (e) {
        case '"': one = '"'; break;
        case '\\': one = '\\'; break;
        case '/': one = '/'; break;
        case 'b': one = '\b'; break;
        case 'f': one = '\f'; break;
        case 'n': one = '\n'; break;
        case 'r': one = '\r'; break;
        case 't': one = '\t'; break;
        case 'u': {
            long cp = hex4(c);
            if (cp < 0)
                return false;
            if (cp >= 0xd800 && cp <= 0xdbff && c->end - c->p >= 6 && c->p[0] == '\\' &&
                c->p[1] == 'u') {
                cursor look = {c->p + 2, c->end};
                long lo = hex4(&look);
                if (lo >= 0xdc00 && lo <= 0xdfff) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                    c->p = look.p;
                }
            }
            char u[4];
            size_t k;
            if (cp >= 0xdc80 && cp <= 0xdcff) {
                u[0] = (char)(cp - 0xdc00);
                k = 1;
            } else if (cp >= 0xd800 && cp <= 0xdfff) {
                return false; /* a lone surrogate that is not a byte */
            } else if (cp < 0x80) {
                u[0] = (char)cp;
                k = 1;
            } else if (cp < 0x800) {
                u[0] = (char)(0xc0 | (cp >> 6));
                u[1] = (char)(0x80 | (cp & 0x3f));
                k = 2;
            } else if (cp < 0x10000) {
                u[0] = (char)(0xe0 | (cp >> 12));
                u[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
                u[2] = (char)(0x80 | (cp & 0x3f));
                k = 3;
            } else {
                u[0] = (char)(0xf0 | (cp >> 18));
                u[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
                u[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
                u[3] = (char)(0x80 | (cp & 0x3f));
                k = 4;
            }
            if (cp == 0 && dst)
                return false; /* no NUL inside a C string */
            if (!emit(dst, cap, &n, u, k, truncate))
                return false;
            continue;
        }
        default:
            return false;
        }
        if (!emit(dst, cap, &n, &one, 1, truncate))
            return false;
    }
    if (dst)
        dst[n] = '\0';
    return true;
}

/* Skips any JSON value (nesting at most 16 deep). */
static bool skip_value(cursor *c, int depth)
{
    skip_ws(c);
    if (c->p >= c->end || depth > 16)
        return false;
    char ch = *c->p;
    if (ch == '"')
        return parse_string(c, NULL, 0, false);
    if (ch == '{' || ch == '[') {
        char close = ch == '{' ? '}' : ']';
        c->p++;
        skip_ws(c);
        if (c->p < c->end && *c->p == close) {
            c->p++;
            return true;
        }
        for (;;) {
            if (ch == '{') {
                skip_ws(c);
                if (!parse_string(c, NULL, 0, false))
                    return false;
                skip_ws(c);
                if (c->p >= c->end || *c->p++ != ':')
                    return false;
            }
            if (!skip_value(c, depth + 1))
                return false;
            skip_ws(c);
            if (c->p >= c->end)
                return false;
            char sep = *c->p++;
            if (sep == close)
                return true;
            if (sep != ',')
                return false;
        }
    }
    if (literal(c, "true") || literal(c, "false") || literal(c, "null"))
        return true;
    const char *start = c->p;
    while (c->p < c->end && *c->p && strchr("+-.eE0123456789", *c->p))
        c->p++;
    return c->p > start;
}

bool xrd_depth_reply_parse(const char *line, size_t len, xrd_depth_reply *out)
{
    memset(out, 0, sizeof *out);
    cursor c = {line, line + len};
    bool have_ok = false, have_depth = false, have_shm = false;
    skip_ws(&c);
    if (c.p >= c.end || *c.p++ != '{')
        return false;
    skip_ws(&c);
    if (c.p < c.end && *c.p == '}') {
        c.p++;
    } else {
        for (;;) {
            char key[32];
            skip_ws(&c);
            if (!parse_string(&c, key, sizeof key, true))
                return false;
            skip_ws(&c);
            if (c.p >= c.end || *c.p++ != ':')
                return false;
            skip_ws(&c);
            bool good;
            if (strcmp(key, "ok") == 0) {
                good = have_ok = true;
                if (literal(&c, "true"))
                    out->ok = true;
                else if (literal(&c, "false"))
                    out->ok = false;
                else
                    good = have_ok = false;
            } else if (strcmp(key, "path") == 0) {
                good = parse_string(&c, out->path, sizeof out->path, false);
            } else if (strcmp(key, "depth") == 0) {
                good = have_depth = parse_string(&c, out->depth, sizeof out->depth, false);
            } else if (strcmp(key, "op") == 0) {
                char op[16];
                good = parse_string(&c, op, sizeof op, true);
                out->video = good && strcmp(op, "video") == 0;
            } else if (strcmp(key, "shm") == 0) {
                good = have_shm = parse_string(&c, out->shm, sizeof out->shm, false);
            } else if (strcmp(key, "error") == 0) {
                good = parse_string(&c, out->error, sizeof out->error, true);
            } else {
                good = skip_value(&c, 0);
            }
            if (!good)
                return false;
            skip_ws(&c);
            if (c.p >= c.end)
                return false;
            char sep = *c.p++;
            if (sep == '}')
                break;
            if (sep != ',')
                return false;
        }
    }
    skip_ws(&c);
    if (c.p != c.end || !have_ok)
        return false;
    if (out->video)
        return have_shm && out->shm[0];
    return !out->ok || (have_depth && out->depth[0]);
}

/* ---- the client --------------------------------------------------------------- */

typedef enum { ST_IDLE, ST_CONNECTING, ST_CONNECTED, ST_FAILED } dc_state;

struct xrd_depthclient {
    char sock[SUN_PATH_LEN];
    char log[XRD_DEPTH_PATH_MAX + 8];
    char helper[XRD_DEPTH_PATH_MAX];
    dc_state st;
    bool pending;        /* fd holds a connect in progress (EINPROGRESS) */
    bool started;        /* this client started the helper */
    bool reported;       /* XRD_DC_FAILED was returned */
    int fd;
    pid_t pid;           /* the helper while it runs; -1 */
    int exit_status;     /* its wait status once it exited; valid when exited */
    bool exited;
    uint64_t start_ns, next_try_ns;
    char *out;
    size_t out_len, out_cap;
    char *in;
    size_t in_len, in_cap;
    char err[640];
};

xrd_depthclient *xrd_depthclient_new(const char *socket_path, const char *helper)
{
    if (strlen(socket_path) >= SUN_PATH_LEN || (helper && strlen(helper) >= XRD_DEPTH_PATH_MAX))
        return NULL;
    xrd_depthclient *dc = calloc(1, sizeof *dc);
    if (!dc)
        return NULL;
    snprintf(dc->sock, sizeof dc->sock, "%s", socket_path);
    if (xrd_depth_log_path(socket_path, dc->log, sizeof dc->log) != 0)
        snprintf(dc->log, sizeof dc->log, "/dev/null");
    if (helper)
        snprintf(dc->helper, sizeof dc->helper, "%s", helper);
    dc->fd = -1;
    dc->pid = -1;
    dc->st = ST_IDLE;
    return dc;
}

static void close_fd(xrd_depthclient *dc)
{
    if (dc->fd >= 0)
        close(dc->fd);
    dc->fd = -1;
    dc->pending = false;
}

void xrd_depthclient_free(xrd_depthclient *dc)
{
    if (!dc)
        return;
    close_fd(dc);
    free(dc->out);
    free(dc->in);
    free(dc);
}

static void fail(xrd_depthclient *dc, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void fail(xrd_depthclient *dc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dc->err, sizeof dc->err, fmt, ap);
    va_end(ap);
    close_fd(dc);
    dc->st = ST_FAILED;
}

static bool append(char **buf, size_t *len, size_t *cap, const char *s, size_t n)
{
    if (*len + n > *cap) {
        size_t c = *cap ? *cap : 1024;
        while (c < *len + n)
            c *= 2;
        char *b = realloc(*buf, c);
        if (!b)
            return false;
        *buf = b;
        *cap = c;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    return true;
}

bool xrd_depthclient_request(xrd_depthclient *dc, const char *path, bool prefetch)
{
    if (!dc || dc->st == ST_FAILED)
        return false;
    char line[2 * XRD_DEPTH_PATH_MAX + 64];
    int n = xrd_depth_request_line(path, prefetch, line, sizeof line);
    if (n < 0)
        return true; /* a path this long gets no depth; the client stays usable */
    if (!append(&dc->out, &dc->out_len, &dc->out_cap, line, (size_t)n)) {
        fail(dc, "out of memory");
        return false;
    }
    if (dc->st == ST_IDLE) {
        dc->st = ST_CONNECTING;
        dc->next_try_ns = 0;
    }
    return true;
}

bool xrd_depthclient_video(xrd_depthclient *dc, const char *shm)
{
    if (!dc || dc->st == ST_FAILED)
        return false;
    char line[128];
    int n = xrd_depth_video_line(shm, line, sizeof line);
    if (n < 0)
        return false;
    if (!append(&dc->out, &dc->out_len, &dc->out_cap, line, (size_t)n)) {
        fail(dc, "out of memory");
        return false;
    }
    if (dc->st == ST_IDLE) {
        dc->st = ST_CONNECTING;
        dc->next_try_ns = 0;
    }
    return true;
}

/* Reaps the helper it started once it exits (no zombie, and the status for
 * the note). */
static void reap(xrd_depthclient *dc)
{
    if (dc->pid <= 0)
        return;
    int status = 0;
    pid_t r = waitpid(dc->pid, &status, WNOHANG);
    if (r == dc->pid || (r < 0 && errno == ECHILD)) {
        dc->exited = r == dc->pid;
        dc->exit_status = status;
        dc->pid = -1;
    }
}

/* "exited with status 2" or "was killed by signal 9". */
static void exit_text(const xrd_depthclient *dc, char *buf, size_t len)
{
    if (!dc->exited)
        snprintf(buf, len, "exited");
    else if (WIFSIGNALED(dc->exit_status))
        snprintf(buf, len, "was killed by signal %d", WTERMSIG(dc->exit_status));
    else
        snprintf(buf, len, "exited with status %d", WEXITSTATUS(dc->exit_status));
}

/* The helper outlives xrdome, so it must not keep xrdome's files open (a
 * display connection, a log): glibc 2.34 closes them with closefrom, macOS
 * with POSIX_SPAWN_CLOEXEC_DEFAULT. Elsewhere only the close-on-exec ones
 * are closed. */
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
static int close_others(posix_spawn_file_actions_t *fa)
{
    return posix_spawn_file_actions_addclosefrom_np(fa, 3);
}
#else
static int close_others(posix_spawn_file_actions_t *fa)
{
    (void)fa;
    return 0;
}
#endif
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
#define SPAWN_CLOEXEC POSIX_SPAWN_CLOEXEC_DEFAULT
#else
#define SPAWN_CLOEXEC 0
#endif

static int start_helper(xrd_depthclient *dc)
{
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    int rc = posix_spawn_file_actions_init(&fa);
    if (rc != 0)
        return rc;
    rc = posix_spawnattr_init(&at);
    if (rc != 0) {
        posix_spawn_file_actions_destroy(&fa);
        return rc;
    }
    sigset_t none, dfl;
    sigemptyset(&none);
    sigemptyset(&dfl);
    sigaddset(&dfl, SIGPIPE);
    /* Its own process group, so Ctrl+C in xrdome's terminal leaves it warm
     * for the next run; it exits by itself when idle. */
    if ((rc = posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0)) == 0 &&
        (rc = posix_spawn_file_actions_addopen(&fa, 1, dc->log, O_WRONLY | O_CREAT | O_TRUNC,
                                               0600)) == 0 &&
        (rc = posix_spawn_file_actions_adddup2(&fa, 1, 2)) == 0 &&
        (rc = close_others(&fa)) == 0 &&
        (rc = posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK |
                                                POSIX_SPAWN_SETSIGDEF | SPAWN_CLOEXEC)) == 0 &&
        (rc = posix_spawnattr_setpgroup(&at, 0)) == 0 &&
        (rc = posix_spawnattr_setsigmask(&at, &none)) == 0 &&
        (rc = posix_spawnattr_setsigdefault(&at, &dfl)) == 0) {
        char *argv[] = {dc->helper, "depth", "serve", "--socket", dc->sock, NULL};
        rc = posix_spawnp(&dc->pid, dc->helper, &fa, &at, argv, environ);
        if (rc != 0)
            dc->pid = -1;
    }
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    return rc;
}

/* One connection attempt: 1 connected (or in progress), 0 nobody answers
 * yet, -1 failed (dc->err set). */
static int try_connect(xrd_depthclient *dc)
{
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    memcpy(sa.sun_path, dc->sock, strlen(dc->sock) + 1);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fail(dc, "cannot create a socket: %s", strerror(errno));
        return -1;
    }
    int fl = fcntl(fd, F_GETFL);
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) != 0) {
        close(fd);
        fail(dc, "cannot set up the socket: %s", strerror(errno));
        return -1;
    }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) {
        dc->fd = fd;
        return 1;
    }
    int e = errno;
    if (e == EINPROGRESS || e == EINTR) {
        dc->fd = fd;
        dc->pending = true;
        return 1;
    }
    close(fd);
    if (e == ENOENT || e == ECONNREFUSED || e == EAGAIN)
        return 0;
    fail(dc, "cannot connect to %s: %s", dc->sock, strerror(e));
    return -1;
}

/* Waiting for the socket: connect, start the helper once, give up when it
 * exits or takes too long. */
static void connect_step(xrd_depthclient *dc, uint64_t now)
{
    if (dc->pending) { /* finish a connect in progress */
        struct pollfd pfd = {dc->fd, POLLOUT, 0};
        if (poll(&pfd, 1, 0) <= 0)
            return;
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        if (getsockopt(dc->fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0)
            soerr = errno;
        if (soerr == 0) {
            dc->pending = false;
            dc->st = ST_CONNECTED;
            return;
        }
        close_fd(dc);
    } else if (now < dc->next_try_ns) {
        return;
    } else {
        int c = try_connect(dc);
        if (c < 0)
            return;
        if (c > 0) {
            if (!dc->pending)
                dc->st = ST_CONNECTED;
            return;
        }
    }
    dc->next_try_ns = now + XRD_DEPTH_RETRY_NS;
    if (!dc->started) {
        if (!dc->helper[0]) {
            fail(dc, "no depth helper answers on %s", dc->sock);
            return;
        }
        int rc = start_helper(dc);
        if (rc != 0) {
            fail(dc, "cannot start %s: %s", dc->helper, strerror(rc));
            return;
        }
        dc->started = true;
        dc->start_ns = now;
        return;
    }
    if (dc->pid < 0) {
        char how[64];
        exit_text(dc, how, sizeof how);
        fail(dc, "%s depth serve %s (its log: %s)", dc->helper, how, dc->log);
    } else if (now - dc->start_ns > XRD_DEPTH_CONNECT_NS) {
        fail(dc, "%s depth serve did not open %s within %llu s (its log: %s)", dc->helper,
             dc->sock, (unsigned long long)(XRD_DEPTH_CONNECT_NS / 1000000000ull), dc->log);
    }
}

static void lost(xrd_depthclient *dc, const char *why)
{
    reap(dc);
    if (dc->started && dc->pid < 0 && dc->exited) {
        char how[64];
        exit_text(dc, how, sizeof how);
        fail(dc, "%s depth serve %s (its log: %s)", dc->helper, how, dc->log);
    } else {
        fail(dc, "the depth helper %s", why);
    }
}

/* Sends what is queued, as far as the socket takes it. */
static void flush_out(xrd_depthclient *dc)
{
    size_t done = 0;
    while (done < dc->out_len) {
        ssize_t n = send(dc->fd, dc->out + done, dc->out_len - done, SEND_FLAGS);
        if (n > 0) {
            done += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        char why[300];
        snprintf(why, sizeof why, "closed the connection: %s", strerror(n < 0 ? errno : EPIPE));
        lost(dc, why);
        return;
    }
    memmove(dc->out, dc->out + done, dc->out_len - done);
    dc->out_len -= done;
}

/* The next whole line in the input as a reply; lines that do not parse are
 * dropped. */
static bool next_reply(xrd_depthclient *dc, xrd_depth_reply *out)
{
    for (;;) {
        char *nl = dc->in_len ? memchr(dc->in, '\n', dc->in_len) : NULL;
        if (!nl)
            return false;
        size_t k = (size_t)(nl - dc->in);
        bool good = xrd_depth_reply_parse(dc->in, k, out);
        memmove(dc->in, nl + 1, dc->in_len - k - 1);
        dc->in_len -= k + 1;
        if (good)
            return true;
    }
}

/* Reads what has come. */
static void read_in(xrd_depthclient *dc)
{
    for (;;) {
        char buf[4096];
        ssize_t n = recv(dc->fd, buf, sizeof buf, 0);
        if (n > 0) {
            if (dc->in_len + (size_t)n > LINE_MAX_BYTES) {
                fail(dc, "the depth helper sent a reply longer than %u bytes", LINE_MAX_BYTES);
                return;
            }
            if (!append(&dc->in, &dc->in_len, &dc->in_cap, buf, (size_t)n)) {
                fail(dc, "out of memory");
                return;
            }
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        char why[300];
        snprintf(why, sizeof why, "closed the connection%s%s", n < 0 ? ": " : "",
                 n < 0 ? strerror(errno) : "");
        lost(dc, why);
        return;
    }
}

xrd_dc_event xrd_depthclient_poll(xrd_depthclient *dc, uint64_t now_ns, xrd_depth_reply *out)
{
    if (!dc)
        return XRD_DC_NONE;
    reap(dc);
    if (dc->st == ST_CONNECTING)
        connect_step(dc, now_ns);
    if (dc->st == ST_CONNECTED && dc->out_len)
        flush_out(dc);
    if (dc->st == ST_CONNECTED)
        read_in(dc);
    if (next_reply(dc, out)) /* replies that came before a close still count */
        return XRD_DC_REPLY;
    if (dc->st == ST_FAILED && !dc->reported) {
        dc->reported = true;
        return XRD_DC_FAILED;
    }
    return XRD_DC_NONE;
}

bool xrd_depthclient_failed(const xrd_depthclient *dc)
{
    return dc && dc->st == ST_FAILED;
}

const char *xrd_depthclient_error(const xrd_depthclient *dc)
{
    return dc ? dc->err : "";
}

bool xrd_depthclient_started(const xrd_depthclient *dc)
{
    return dc && dc->started;
}
