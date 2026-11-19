#ifndef XRD_APP_DEPTHCLIENT_H
#define XRD_APP_DEPTHCLIENT_H

/* The player's side of the depth helper, `xrdome-tools depth serve` (spec 7
 * of docs/superpowers/specs/2026-11-19-depth-3d-design.md;
 * tools/xrdome_depth/serve.py is the other side). A Unix stream socket, one
 * JSON object per line:
 *
 *   request  {"op": "photo", "path": "<absolute path>", "prefetch": false}
 *   reply    {"ok": true, "path": "<as asked>", "depth": "<cache file>"}
 *            {"ok": false, "path": "<as asked>", "error": "..."}
 *
 * Paths travel as their bytes (UTF-8 JSON; other bytes as they are). Every
 * call here is non-blocking: the render loop polls once per frame. The helper
 * is started, the first time a request needs it and its socket does not
 * answer, as "<helper> depth serve --socket <path>" with its output in the
 * log file beside the socket; it outlives xrdome and exits by itself when
 * idle. No SDL, GL or mpv. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XRD_DEPTH_PATH_MAX 4096
#define XRD_DEPTH_CONNECT_NS 30000000000ull /* a started helper must answer within 30 s */
#define XRD_DEPTH_RETRY_NS 50000000ull      /* between connection attempts while it starts */

/* $XDG_RUNTIME_DIR/xrdome-depth.sock; without an absolute XDG_RUNTIME_DIR
 * (macOS has none), xrdome-depth-<uid>.sock in $TMPDIR when it is absolute,
 * else in /tmp (as xrdome_tools.depth.socket_path). -1 when it does not fit
 * len or a Unix socket address. */
int xrd_depth_socket_path(char *buf, size_t len);

/* The helper's log beside the socket: the socket path with ".sock" replaced by
 * ".log" (or ".log" appended). -1 if it does not fit. */
int xrd_depth_log_path(const char *socket_path, char *buf, size_t len);

/* The absolute path the helper is asked for (realpath: its working directory
 * is not xrdome's). -1 with errno set when path cannot be resolved or does
 * not fit. */
int xrd_depth_abs_path(const char *path, char *buf, size_t len);

/* One request line, newline included, NUL terminated: its length, or -1 when
 * it does not fit len. */
int xrd_depth_request_line(const char *path, bool prefetch, char *buf, size_t len);

typedef struct {
    bool ok;
    char path[XRD_DEPTH_PATH_MAX];  /* the request it answers; "" when the reply has none */
    char depth[XRD_DEPTH_PATH_MAX]; /* ok: the cache file */
    char error[512];                /* !ok: why */
} xrd_depth_reply;

/* Parses one reply line (without the newline). False for anything that is not
 * a JSON object with a boolean "ok", strings that do not fit, or an ok reply
 * without "depth". Unknown keys are skipped. */
bool xrd_depth_reply_parse(const char *line, size_t len, xrd_depth_reply *out);

typedef struct xrd_depthclient xrd_depthclient;

/* A client of the helper at socket_path. helper: the program to start when
 * the socket does not answer, NULL to only use one that is running. Nothing
 * happens until the first request. NULL when out of memory. */
xrd_depthclient *xrd_depthclient_new(const char *socket_path, const char *helper);
/* Closes the connection; a helper it started keeps running. NULL is fine. */
void xrd_depthclient_free(xrd_depthclient *dc);

/* Queues a request for the absolute path. False once the client has failed. */
bool xrd_depthclient_request(xrd_depthclient *dc, const char *path, bool prefetch);

typedef enum {
    XRD_DC_NONE,   /* nothing new */
    XRD_DC_REPLY,  /* *out holds a reply; poll again for more */
    XRD_DC_FAILED, /* the helper is lost (xrd_depthclient_error); returned once */
} xrd_dc_event;

/* Connects (starting the helper when needed), sends what is queued and reads
 * what has come, without waiting. now_ns: a monotonic clock. */
xrd_dc_event xrd_depthclient_poll(xrd_depthclient *dc, uint64_t now_ns, xrd_depth_reply *out);

bool xrd_depthclient_failed(const xrd_depthclient *dc);
/* Why it failed ("" before): one line, naming the log when the helper died. */
const char *xrd_depthclient_error(const xrd_depthclient *dc);
/* Whether this client started the helper process. */
bool xrd_depthclient_started(const xrd_depthclient *dc);

#endif
