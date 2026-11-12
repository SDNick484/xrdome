#ifndef XRD_UTIL_TIMING_H
#define XRD_UTIL_TIMING_H

#include <stdint.h>

/* CLOCK_MONOTONIC in nanoseconds. */
uint64_t xrd_now_ns(void);

/* Frame pacing for when the swap does not wait for vsync (swap interval
 * unavailable or ignored by the driver). Call right after the swap.
 * prev_swap_ns: time after the previous swap (and any pacing sleep), 0 for
 * the first frame. Returns how long to sleep: 0 when the swap already paced
 * the frame (interval >= period / 2), otherwise until prev_swap_ns + period,
 * never more than one period. */
uint64_t xrd_pace_sleep_ns(uint64_t prev_swap_ns, uint64_t now_ns, uint64_t period_ns);
/* 1e9 / hz rounded; hz <= 0 (unknown) means 60. */
uint64_t xrd_refresh_period_ns(int hz);
/* Sleeps ns nanoseconds on CLOCK_MONOTONIC (0 returns at once). */
void xrd_sleep_ns(uint64_t ns);

/* Swap intervals above this count as a missed vsync. */
#define XRD_MISSED_VSYNC_NS 20000000ull

/* Per-frame timing statistics with an optional CSV log. */
typedef struct xrd_timing xrd_timing;

/* NULL path = stats only, no file. Returns NULL when the file cannot be
 * created (errno is set) or on out of memory. */
xrd_timing *xrd_timing_open(const char *csv_path);
/* One call per frame, just before the swap call. All times are
 * CLOCK_MONOTONIC ns: pose_sample_ns when the view was taken from the
 * tracker, pre_swap_ns just before the swap, pose_t_mono_ns the receipt time
 * of the pose used (0 = no pose yet), swap_ns the time spent inside the swap
 * call (plus glFinish with --sync-after-swap). CSV row:
 * frame,interval_us,pose_to_swap_us,pose_age_us,swap_us, where interval_us is
 * 0 for the first frame and pose_age_us is 0 without a pose. */
void xrd_timing_frame(xrd_timing *t, uint64_t pose_sample_ns, uint64_t pre_swap_ns,
                      uint64_t pose_t_mono_ns, uint64_t swap_ns);
unsigned xrd_timing_missed_vsyncs(const xrd_timing *t);   /* swap-to-swap intervals > 20 ms */
uint64_t xrd_timing_max_pose_to_swap_ns(const xrd_timing *t);
/* Median pose-to-swap over the most recent XRD_TIMING_MEDIAN_SAMPLES frames
 * (the upper median for an even count); 0 before the first frame. The max
 * above covers every frame. */
#define XRD_TIMING_MEDIAN_SAMPLES 4096u
uint64_t xrd_timing_median_pose_to_swap_ns(const xrd_timing *t);
/* Prints a one-line summary to stderr, closes the CSV and frees. NULL is
 * accepted. */
void xrd_timing_close(xrd_timing *t);

#endif
