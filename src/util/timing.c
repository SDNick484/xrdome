#define _POSIX_C_SOURCE 200809L /* clock_gettime */

#include "util/timing.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

uint64_t xrd_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

struct xrd_timing {
    FILE *csv;          /* NULL = stats only */
    uint64_t frames;
    uint64_t prev_swap_ns;
    unsigned missed;
    uint64_t max_pose_to_swap_ns;
};

/* a - b, or 0 when b is later than a. */
static uint64_t sub_sat(uint64_t a, uint64_t b)
{
    return a > b ? a - b : 0;
}

xrd_timing *xrd_timing_open(const char *csv_path)
{
    xrd_timing *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;
    if (csv_path) {
        t->csv = fopen(csv_path, "w");
        if (!t->csv) {
            free(t);
            return NULL;
        }
        /* Line buffered so the log can be followed live (tail -f, the
         * tracking smoke test); one short write per frame. */
        setvbuf(t->csv, NULL, _IOLBF, 0);
        fputs("frame,interval_us,pose_to_swap_us,pose_age_us\n", t->csv);
    }
    return t;
}

void xrd_timing_frame(xrd_timing *t, uint64_t pose_sample_ns, uint64_t pre_swap_ns,
                      uint64_t pose_t_mono_ns)
{
    uint64_t interval = t->frames ? sub_sat(pre_swap_ns, t->prev_swap_ns) : 0;
    uint64_t pose_to_swap = sub_sat(pre_swap_ns, pose_sample_ns);
    uint64_t pose_age = pose_t_mono_ns ? sub_sat(pre_swap_ns, pose_t_mono_ns) : 0;

    if (interval > XRD_MISSED_VSYNC_NS)
        t->missed++;
    if (pose_to_swap > t->max_pose_to_swap_ns)
        t->max_pose_to_swap_ns = pose_to_swap;
    if (t->csv)
        fprintf(t->csv, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", t->frames,
                interval / 1000, pose_to_swap / 1000, pose_age / 1000);
    t->prev_swap_ns = pre_swap_ns;
    t->frames++;
}

unsigned xrd_timing_missed_vsyncs(const xrd_timing *t)
{
    return t->missed;
}

uint64_t xrd_timing_max_pose_to_swap_ns(const xrd_timing *t)
{
    return t->max_pose_to_swap_ns;
}

void xrd_timing_close(xrd_timing *t)
{
    if (!t)
        return;
    fprintf(stderr, "timing: %" PRIu64 " frames, %u missed vsyncs (swap interval > 20 ms), "
                    "max pose-to-swap %.3f ms\n",
            t->frames, t->missed, (double)t->max_pose_to_swap_ns / 1e6);
    if (t->csv && fclose(t->csv) != 0)
        fprintf(stderr, "xrdome: warning: error writing the timing log\n");
    free(t);
}
