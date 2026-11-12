#define _POSIX_C_SOURCE 200809L /* clock_gettime */

#include "util/timing.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    uint64_t ring[XRD_TIMING_MEDIAN_SAMPLES]; /* recent pose_to_swap_ns, circular */
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
        fputs("frame,interval_us,pose_to_swap_us,pose_age_us,swap_us\n", t->csv);
    }
    return t;
}

void xrd_timing_frame(xrd_timing *t, uint64_t pose_sample_ns, uint64_t pre_swap_ns,
                      uint64_t pose_t_mono_ns, uint64_t swap_ns)
{
    uint64_t interval = t->frames ? sub_sat(pre_swap_ns, t->prev_swap_ns) : 0;
    uint64_t pose_to_swap = sub_sat(pre_swap_ns, pose_sample_ns);
    uint64_t pose_age = pose_t_mono_ns ? sub_sat(pre_swap_ns, pose_t_mono_ns) : 0;

    if (interval > XRD_MISSED_VSYNC_NS)
        t->missed++;
    if (pose_to_swap > t->max_pose_to_swap_ns)
        t->max_pose_to_swap_ns = pose_to_swap;
    t->ring[t->frames % XRD_TIMING_MEDIAN_SAMPLES] = pose_to_swap;
    if (t->csv)
        fprintf(t->csv, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
                t->frames, interval / 1000, pose_to_swap / 1000, pose_age / 1000,
                swap_ns / 1000);
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

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

uint64_t xrd_timing_median_pose_to_swap_ns(const xrd_timing *t)
{
    size_t n = t->frames < XRD_TIMING_MEDIAN_SAMPLES ? (size_t)t->frames
                                                      : XRD_TIMING_MEDIAN_SAMPLES;
    if (n == 0)
        return 0;
    uint64_t *tmp = malloc(n * sizeof *tmp);
    if (!tmp)
        return 0;
    memcpy(tmp, t->ring, n * sizeof *tmp); /* before it wraps, the first n slots are the samples */
    qsort(tmp, n, sizeof *tmp, cmp_u64);
    uint64_t m = tmp[n / 2];
    free(tmp);
    return m;
}

void xrd_timing_close(xrd_timing *t)
{
    if (!t)
        return;
    fprintf(stderr, "timing: %" PRIu64 " frames, %u missed vsyncs (swap interval > 20 ms), "
                    "pose-to-swap max %.3f ms, median %.3f ms (last %u frames)\n",
            t->frames, t->missed, (double)t->max_pose_to_swap_ns / 1e6,
            (double)xrd_timing_median_pose_to_swap_ns(t) / 1e6, XRD_TIMING_MEDIAN_SAMPLES);
    if (t->csv && fclose(t->csv) != 0)
        fprintf(stderr, "xrdome: warning: error writing the timing log\n");
    free(t);
}

uint64_t xrd_pace_sleep_ns(uint64_t prev_swap_ns, uint64_t now_ns, uint64_t period_ns)
{
    if (prev_swap_ns == 0 || period_ns == 0)
        return 0;
    uint64_t interval = sub_sat(now_ns, prev_swap_ns);
    if (interval >= period_ns / 2)
        return 0; /* the swap waited for vsync (or the frame was late) */
    uint64_t target = prev_swap_ns + period_ns;
    uint64_t sleep = sub_sat(target, now_ns);
    return sleep > period_ns ? period_ns : sleep;
}

uint64_t xrd_refresh_period_ns(int hz)
{
    if (hz <= 0)
        hz = 60;
    return (1000000000ull + (uint64_t)hz / 2) / (uint64_t)hz;
}

void xrd_sleep_ns(uint64_t ns)
{
    if (ns == 0)
        return;
    struct timespec ts = {.tv_sec = (time_t)(ns / 1000000000ull),
                          .tv_nsec = (long)(ns % 1000000000ull)};
    while (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts) == EINTR)
        ;
}
