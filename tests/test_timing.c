#define _POSIX_C_SOURCE 200809L /* mkstemp */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

#include "util/timing.h"

#define MS(x) ((uint64_t)((x) * 1000000.0 + 0.5))

static void missed_vsyncs_counts_intervals_over_20ms(void **state)
{
    (void)state;
    xrd_timing *t = xrd_timing_open(NULL);
    assert_non_null(t);
    const double pre_swap_ms[] = {0.0, 16.7, 33.4, 60.0, 76.7};
    /* Offset by 1 s so pose times stay positive. */
    for (size_t i = 0; i < sizeof pre_swap_ms / sizeof pre_swap_ms[0]; i++) {
        uint64_t s = MS(1000.0 + pre_swap_ms[i]);
        xrd_timing_frame(t, s - MS(1), s, s - MS(2));
    }
    assert_int_equal(xrd_timing_missed_vsyncs(t), 1);
    xrd_timing_close(t);
}

static void exactly_20ms_is_not_missed(void **state)
{
    (void)state;
    xrd_timing *t = xrd_timing_open(NULL);
    xrd_timing_frame(t, MS(1000), MS(1000), 0);
    xrd_timing_frame(t, MS(1020), MS(1020), 0);
    assert_int_equal(xrd_timing_missed_vsyncs(t), 0);
    xrd_timing_frame(t, MS(1040) + 1, MS(1040) + 1, 0);
    assert_int_equal(xrd_timing_missed_vsyncs(t), 1);
    xrd_timing_close(t);
}

static void max_pose_to_swap(void **state)
{
    (void)state;
    xrd_timing *t = xrd_timing_open(NULL);
    assert_int_equal(xrd_timing_max_pose_to_swap_ns(t), 0);
    xrd_timing_frame(t, MS(1000), MS(1001), MS(999));      /* 1 ms */
    xrd_timing_frame(t, MS(1013.7), MS(1016.7), MS(1010)); /* 3 ms */
    xrd_timing_frame(t, MS(1033), MS(1033.4), MS(1030));   /* 0.4 ms */
    assert_true(xrd_timing_max_pose_to_swap_ns(t) == 3000000u);
    xrd_timing_close(t);
}

static int count_lines(const char *path, char *first, size_t firstlen, char *last, size_t lastlen)
{
    FILE *f = fopen(path, "r");
    assert_non_null(f);
    char buf[256];
    int n = 0;
    while (fgets(buf, sizeof buf, f)) {
        if (n == 0)
            snprintf(first, firstlen, "%s", buf);
        snprintf(last, lastlen, "%s", buf);
        n++;
    }
    fclose(f);
    return n;
}

static void csv_has_header_and_one_line_per_frame(void **state)
{
    (void)state;
    char path[] = "/tmp/xrd_timing_XXXXXX";
    int fd = mkstemp(path);
    assert_true(fd >= 0);
    close(fd);

    xrd_timing *t = xrd_timing_open(path);
    assert_non_null(t);
    xrd_timing_frame(t, MS(1000), MS(1001), 0);           /* no pose yet */
    xrd_timing_frame(t, MS(1015.7), MS(1017.7), MS(1012)); /* 16.7 ms later */
    xrd_timing_frame(t, MS(1032), MS(1034.4), MS(1030));
    xrd_timing_close(t);

    char first[256], last[256];
    int n = count_lines(path, first, sizeof first, last, sizeof last);
    unlink(path);
    assert_int_equal(n, 4);
    assert_string_equal(first, "frame,interval_us,pose_to_swap_us,pose_age_us\n");
    assert_string_equal(last, "2,16700,2400,4400\n");
}

static void csv_first_frame_and_missing_pose_are_zero(void **state)
{
    (void)state;
    char path[] = "/tmp/xrd_timing_XXXXXX";
    int fd = mkstemp(path);
    assert_true(fd >= 0);
    close(fd);

    xrd_timing *t = xrd_timing_open(path);
    assert_non_null(t);
    xrd_timing_frame(t, MS(1000), MS(1001.5), 0);
    xrd_timing_close(t);

    char first[256], last[256];
    int n = count_lines(path, first, sizeof first, last, sizeof last);
    unlink(path);
    assert_int_equal(n, 2);
    assert_string_equal(last, "0,0,1500,0\n");
}

static void open_unwritable_path_fails(void **state)
{
    (void)state;
    assert_null(xrd_timing_open("/nonexistent-dir/xrd.csv"));
}

static void close_null_is_ok(void **state)
{
    (void)state;
    xrd_timing_close(NULL);
}

#define PERIOD MS(1000.0 / 60.0)

static void pace_first_frame_never_sleeps(void **state)
{
    (void)state;
    assert_true(xrd_pace_sleep_ns(0, MS(1000), PERIOD) == 0);
}

static void pace_vsync_paced_frame_does_not_sleep(void **state)
{
    (void)state;
    assert_true(xrd_pace_sleep_ns(MS(1000), MS(1016.7), PERIOD) == 0);
    /* a late frame does not sleep either */
    assert_true(xrd_pace_sleep_ns(MS(1000), MS(1040), PERIOD) == 0);
    /* exactly half a period counts as paced */
    assert_true(xrd_pace_sleep_ns(MS(1000), MS(1000) + PERIOD / 2, PERIOD) == 0);
}

static void pace_unpaced_frame_sleeps_to_next_period(void **state)
{
    (void)state;
    /* swap returned 1 ms after the previous one: sleep until prev + period */
    assert_true(xrd_pace_sleep_ns(MS(1000), MS(1001), PERIOD) == MS(1000) + PERIOD - MS(1001));
    assert_true(xrd_pace_sleep_ns(MS(1000), MS(1000), PERIOD) == PERIOD);
    assert_true(xrd_pace_sleep_ns(MS(1000), MS(1000) + PERIOD / 2 - 1, PERIOD) ==
                PERIOD - PERIOD / 2 + 1);
}

static void pace_clock_going_backwards_sleeps_at_most_a_period(void **state)
{
    (void)state;
    assert_true(xrd_pace_sleep_ns(MS(1000), MS(990), PERIOD) == PERIOD);
}

static void pace_zero_period_never_sleeps(void **state)
{
    (void)state;
    assert_true(xrd_pace_sleep_ns(MS(1000), MS(1001), 0) == 0);
}

static void refresh_period_from_hz(void **state)
{
    (void)state;
    assert_true(xrd_refresh_period_ns(60) == 16666667u);
    assert_true(xrd_refresh_period_ns(120) == 8333333u);
    /* unknown rate (SDL reports 0): assume 60 Hz */
    assert_true(xrd_refresh_period_ns(0) == 16666667u);
    assert_true(xrd_refresh_period_ns(-5) == 16666667u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(missed_vsyncs_counts_intervals_over_20ms),
        cmocka_unit_test(exactly_20ms_is_not_missed),
        cmocka_unit_test(max_pose_to_swap),
        cmocka_unit_test(csv_has_header_and_one_line_per_frame),
        cmocka_unit_test(csv_first_frame_and_missing_pose_are_zero),
        cmocka_unit_test(open_unwritable_path_fails),
        cmocka_unit_test(close_null_is_ok),
        cmocka_unit_test(pace_first_frame_never_sleeps),
        cmocka_unit_test(pace_vsync_paced_frame_does_not_sleep),
        cmocka_unit_test(pace_unpaced_frame_sleeps_to_next_period),
        cmocka_unit_test(pace_clock_going_backwards_sleeps_at_most_a_period),
        cmocka_unit_test(pace_zero_period_never_sleeps),
        cmocka_unit_test(refresh_period_from_hz),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
