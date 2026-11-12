#ifndef XRD_UTIL_TIMING_H
#define XRD_UTIL_TIMING_H

#include <stdint.h>

/* CLOCK_MONOTONIC in nanoseconds. */
uint64_t xrd_now_ns(void);

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
 * of the pose used (0 = no pose yet). CSV row:
 * frame,interval_us,pose_to_swap_us,pose_age_us, where interval_us is 0 for
 * the first frame and pose_age_us is 0 without a pose. */
void xrd_timing_frame(xrd_timing *t, uint64_t pose_sample_ns, uint64_t pre_swap_ns,
                      uint64_t pose_t_mono_ns);
unsigned xrd_timing_missed_vsyncs(const xrd_timing *t);   /* swap-to-swap intervals > 20 ms */
uint64_t xrd_timing_max_pose_to_swap_ns(const xrd_timing *t);
/* Prints a one-line summary to stderr, closes the CSV and frees. NULL is
 * accepted. */
void xrd_timing_close(xrd_timing *t);

#endif
