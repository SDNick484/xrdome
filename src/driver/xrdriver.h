#ifndef XRD_DRIVER_XRDRIVER_H
#define XRD_DRIVER_XRDRIVER_H

/* XRLinuxDriver's shared-memory files, read and written by xrdome without
 * linking or copying driver code (format from the driver's src/state.c):
 *
 *   <dir>/xr_driver_state    key=value lines the driver rewrites every second,
 *                            among them heartbeat=<epoch seconds> and, while
 *                            glasses are connected, sbs_mode_supported= and
 *                            sbs_mode_enabled= (true or false).
 *   <dir>/xr_driver_control  one-shot requests, key=value lines. The driver
 *                            reads the file when a writer closes it (inotify
 *                            IN_CLOSE_WRITE on that name), then deletes it.
 *
 * <dir> is /dev/shm, or $XRDOME_XR_DRIVER_DIR when set and not empty (tests
 * point it at a temporary directory so nothing reaches a real driver). */

#include <stdbool.h>
#include <stddef.h>

#ifndef XRD_XRDRIVER_DIR_DEFAULT
#define XRD_XRDRIVER_DIR_DEFAULT "/dev/shm"
#endif
#define XRD_XRDRIVER_DIR_ENV "XRDOME_XR_DRIVER_DIR"
#define XRD_XRDRIVER_STATE   "xr_driver_state"
#define XRD_XRDRIVER_CONTROL "xr_driver_control"
#define XRD_XRDRIVER_PATH_MAX 256 /* buffer size for xrd_xrdriver_path */

/* A heartbeat at most this many seconds from now (either way) is fresh: the
 * driver writes one every second. */
#define XRD_XRDRIVER_FRESH_S 5

typedef struct {
    bool have_heartbeat;
    long long heartbeat;  /* epoch seconds; valid when have_heartbeat */
    bool have_sbs;        /* both sbs_mode_ lines were present (glasses connected) */
    bool sbs_supported;
    bool sbs_enabled;
} xrd_xrdriver_state;

/* The driver's directory: $XRDOME_XR_DRIVER_DIR when set and not empty,
 * otherwise XRD_XRDRIVER_DIR_DEFAULT. */
const char *xrd_xrdriver_dir(void);

/* Writes "<dir>/<file>" into buf. Returns 0, or -1 when it does not fit. */
int xrd_xrdriver_path(const char *file, char *buf, size_t len);

/* Parses the state file's text. Unknown keys, and values other than true and
 * false for the sbs_mode_ keys, are ignored. */
void xrd_xrdriver_parse_state(const char *text, xrd_xrdriver_state *st);

/* Reads and parses the state file at path. Returns 0, or -1 with errno set
 * when it cannot be opened or read. An empty file (caught between the
 * driver's truncate and write) is read again a few times before it counts. */
int xrd_xrdriver_read_state(const char *path, xrd_xrdriver_state *st);

/* True when the heartbeat is within XRD_XRDRIVER_FRESH_S of now. */
bool xrd_xrdriver_fresh(const xrd_xrdriver_state *st, long long now);

/* The sbs_mode request line: "sbs_mode=enable\n" or "sbs_mode=disable\n". */
const char *xrd_xrdriver_sbs_request(bool enable);

/* Writes text as one request to the control file at path: opened in place
 * (O_CREAT|O_TRUNC, never through a symlink), written in one go and closed,
 * which is the close the driver waits for. Returns 0, or -1 with errno. */
int xrd_xrdriver_request(const char *path, const char *text);

#endif
