#ifndef XRD_DISPLAY_SBS_H
#define XRD_DISPLAY_SBS_H

/* Switching the glasses to side-by-side through XRLinuxDriver when no
 * 3840x1080 output is found at start, and back at exit (--sbs-switch auto). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "display/output.h"
#include "driver/xrdriver.h"

#define XRD_SBS_WAIT_MS 8000 /* how long the 3840x1080 output may take to appear */
#define XRD_SBS_POLL_MS 200  /* re-enumerate at least this often while waiting */

typedef enum {
    XRD_SBS_GO = 0,      /* ask the driver to switch */
    XRD_SBS_NO_DRIVER,   /* no readable state file */
    XRD_SBS_STALE,       /* no heartbeat, or one older than XRD_XRDRIVER_FRESH_S */
    XRD_SBS_NO_GLASSES,  /* the driver lists no glasses (no sbs_mode_ lines) */
    XRD_SBS_UNSUPPORTED, /* sbs_mode_supported=false */
    XRD_SBS_ALREADY_ON,  /* sbs_mode_enabled=true */
} xrd_sbs_verdict;

/* Whether to ask for the switch, from the driver's state (NULL when the
 * state file could not be read) and the wall clock in epoch seconds. */
xrd_sbs_verdict xrd_sbs_decide(const xrd_xrdriver_state *st, long long now);

/* Why a verdict other than XRD_SBS_GO does not switch, as one phrase
 * naming the state file where it helps. Always NUL-terminated. */
void xrd_sbs_reason(xrd_sbs_verdict v, const xrd_xrdriver_state *st, long long now,
                    const char *state_path, char *buf, size_t len);

/* What the switch needs from the display system and the clocks; tests pass
 * fakes, xrd_sbs_sdl_ops() the real thing. */
typedef struct {
    /* A fresh list of outputs: count, or negative on error (retried). */
    int (*enumerate)(void *ctx, xrd_output_info *outs, int max);
    /* Waits up to ms for display changes; false when the user asked to quit. */
    bool (*wait)(void *ctx, int ms);
    uint64_t (*now_ms)(void *ctx); /* monotonic milliseconds */
    long long (*epoch_s)(void *ctx); /* wall clock, for the heartbeat */
} xrd_sbs_ops;

/* SDL: waits on SDL events and re-initialises the video subsystem to list
 * the outputs afresh (SDL keeps a display's old mode across a mode change).
 * Only valid before any window exists. */
const xrd_sbs_ops *xrd_sbs_sdl_ops(void);

typedef enum {
    XRD_SBS_NOT_ASKED = 0, /* the driver was not asked (see why) */
    XRD_SBS_SWITCHED,      /* asked, and a 3840x1080 output appeared */
    XRD_SBS_TIMED_OUT,     /* asked, nothing appeared within the wait */
    XRD_SBS_QUIT,          /* asked, and the user quit while waiting */
} xrd_sbs_outcome;

typedef struct {
    xrd_sbs_outcome outcome;
    uint64_t waited_ms; /* request to output, when switched */
    char why[512];      /* not switched: why, as one phrase */
} xrd_sbs_result;

/* Called when xrd_output_select(outs, *n, want, ...) has failed and
 * xrd_output_sbs_candidate holds. Reads the driver's state, and when it
 * allows, writes sbs_mode=enable to the control file and waits up to
 * timeout_ms for the output to appear, re-enumerating into outs and *n.
 * Returns XRD_OUT_OK with *idx set when it did, otherwise the selection
 * result from the last list. */
int xrd_sbs_switch(const xrd_sbs_ops *ops, void *ctx, const char *want, int timeout_ms,
                   xrd_output_info *outs, int max, int *n, int *idx, xrd_sbs_result *res);

/* At exit, after XRD_SBS_SWITCHED: asks the driver to switch the glasses
 * back, unless its state is no longer fresh (the request would wait for the
 * next driver start). Returns true when the request was written; otherwise
 * why says why not. */
bool xrd_sbs_restore(long long now, char *why, size_t len);

#endif
