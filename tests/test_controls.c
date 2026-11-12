#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <math.h>
#include <stdbool.h>
#include <cmocka.h>

#include "app/controls.h"
#include "math/quat.h"

#define TOL 1e-4

static void init_copies_start_values(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.5f, XRD_LAYOUT_SBS_RL);
    assert_float_equal(c.zoom, 1.5f, TOL);
    assert_int_equal(c.layout, XRD_LAYOUT_SBS_RL);
    assert_false(c.swap_eyes);
    assert_float_equal(c.mouse_yaw_deg, 0.0, TOL);
    assert_float_equal(c.mouse_pitch_deg, 0.0, TOL);
}

static void zoom_steps_by_1_1(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_zoom_in(&c);
    assert_float_equal(c.zoom, 1.1f, TOL);
    xrd_controls_zoom_in(&c);
    assert_float_equal(c.zoom, 1.21f, TOL);
    xrd_controls_zoom_out(&c);
    xrd_controls_zoom_out(&c);
    assert_float_equal(c.zoom, 1.0f, TOL);
    xrd_controls_zoom_out(&c);
    assert_float_equal(c.zoom, 1.0f / 1.1f, TOL);
}

static void zoom_clamps_to_quarter_and_four(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    for (int i = 0; i < 100; i++)
        xrd_controls_zoom_in(&c);
    assert_float_equal(c.zoom, 4.0f, TOL);
    for (int i = 0; i < 100; i++)
        xrd_controls_zoom_out(&c);
    assert_float_equal(c.zoom, 0.25f, TOL);

    /* one step from just inside the limit lands exactly on it */
    xrd_controls_init(&c, 3.9f, XRD_LAYOUT_SBS_LR);
    xrd_controls_zoom_in(&c);
    assert_float_equal(c.zoom, 4.0f, TOL);
    xrd_controls_init(&c, 0.26f, XRD_LAYOUT_SBS_LR);
    xrd_controls_zoom_out(&c);
    assert_float_equal(c.zoom, 0.25f, TOL);
}

static void layout_cycles_lr_rl_mono(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_cycle_layout(&c);
    assert_int_equal(c.layout, XRD_LAYOUT_SBS_RL);
    xrd_controls_cycle_layout(&c);
    assert_int_equal(c.layout, XRD_LAYOUT_MONO);
    xrd_controls_cycle_layout(&c);
    assert_int_equal(c.layout, XRD_LAYOUT_SBS_LR);

    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_MONO);
    xrd_controls_cycle_layout(&c);
    assert_int_equal(c.layout, XRD_LAYOUT_SBS_LR);
}

static void swap_toggles(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_toggle_swap(&c);
    assert_true(c.swap_eyes);
    xrd_controls_toggle_swap(&c);
    assert_false(c.swap_eyes);
}

static void drag_right_looks_right_drag_up_looks_up(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    /* SDL: +x is right, +y is down. Positive yaw is a left turn. */
    xrd_controls_mouse_drag(&c, 100, 0);
    assert_float_equal(c.mouse_yaw_deg, -10.0, TOL);
    assert_float_equal(c.mouse_pitch_deg, 0.0, TOL);
    xrd_controls_mouse_drag(&c, 0, -50);
    assert_float_equal(c.mouse_pitch_deg, 5.0, TOL);
    xrd_controls_mouse_drag(&c, -30, 20);
    assert_float_equal(c.mouse_yaw_deg, -7.0, TOL);
    assert_float_equal(c.mouse_pitch_deg, 3.0, TOL);

    /* The resulting rotation turns the forward axis right and up. */
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_mouse_drag(&c, 100, -100);
    const float fwd[3] = {0.0f, 0.0f, -1.0f};
    float d[3];
    xrd_quat_rotate(xrd_controls_mouse_quat(&c), fwd, d);
    assert_true(d[0] > 0.1f); /* right */
    assert_true(d[1] > 0.1f); /* up */
}

static void pitch_clamps_to_89(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_mouse_drag(&c, 0, -5000);
    assert_float_equal(c.mouse_pitch_deg, 89.0, TOL);
    xrd_controls_mouse_drag(&c, 0, 10);
    assert_float_equal(c.mouse_pitch_deg, 88.0, TOL);
    xrd_controls_mouse_drag(&c, 0, 5000);
    assert_float_equal(c.mouse_pitch_deg, -89.0, TOL);
}

static void yaw_is_not_clamped(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_mouse_drag(&c, -2000, 0);
    assert_float_equal(c.mouse_yaw_deg, 200.0, TOL);
}

static void recenter_zeroes_mouse_angles_only(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_mouse_drag(&c, 123, -45);
    xrd_controls_zoom_in(&c);
    xrd_controls_toggle_swap(&c);
    xrd_controls_recenter(&c);
    assert_float_equal(c.mouse_yaw_deg, 0.0, TOL);
    assert_float_equal(c.mouse_pitch_deg, 0.0, TOL);
    assert_float_equal(c.zoom, 1.1f, TOL);
    assert_true(c.swap_eyes);

    xrd_quat q = xrd_controls_mouse_quat(&c);
    assert_float_equal(fabsf(q.w), 1.0f, TOL);
}

static void init_display_defaults_and_values(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    assert_float_equal(c.convergence_deg, 0.0f, TOL);
    assert_int_equal(c.filter, XRD_FILTER_BICUBIC);
    xrd_controls_init_display(&c, 1.5f, XRD_FILTER_BILINEAR);
    assert_float_equal(c.convergence_deg, 1.5f, TOL);
    assert_int_equal(c.filter, XRD_FILTER_BILINEAR);
}

static void convergence_steps_by_tenth(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_convergence_inc(&c);
    assert_float_equal(c.convergence_deg, 0.1f, TOL);
    for (int i = 0; i < 9; i++)
        xrd_controls_convergence_inc(&c);
    assert_float_equal(c.convergence_deg, 1.0f, TOL);
    for (int i = 0; i < 13; i++)
        xrd_controls_convergence_dec(&c);
    assert_float_equal(c.convergence_deg, -0.3f, TOL);
    for (int i = 0; i < 3; i++)
        xrd_controls_convergence_inc(&c);
    /* back at zero after many steps: exactly zero, not a negative zero */
    assert_true(c.convergence_deg == 0.0f);
    assert_false(signbit(c.convergence_deg));
}

static void convergence_clamps_to_5(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    for (int i = 0; i < 100; i++)
        xrd_controls_convergence_inc(&c);
    assert_float_equal(c.convergence_deg, 5.0f, TOL);
    assert_true(c.convergence_deg <= 5.0f);
    for (int i = 0; i < 200; i++)
        xrd_controls_convergence_dec(&c);
    assert_float_equal(c.convergence_deg, -5.0f, TOL);
    assert_true(c.convergence_deg >= -5.0f);
    /* from just inside the limit a step lands exactly on it */
    xrd_controls_init_display(&c, 4.95f, XRD_FILTER_BICUBIC);
    xrd_controls_convergence_inc(&c);
    assert_true(c.convergence_deg == 5.0f);
}

static void convergence_reset_returns_to_configured(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_init_display(&c, 2.0f, XRD_FILTER_BICUBIC);
    for (int i = 0; i < 7; i++)
        xrd_controls_convergence_dec(&c);
    assert_float_equal(c.convergence_deg, 1.3f, TOL);
    xrd_controls_convergence_reset(&c);
    assert_float_equal(c.convergence_deg, 2.0f, TOL);

    /* default configuration resets to 0 */
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_convergence_inc(&c);
    xrd_controls_convergence_reset(&c);
    assert_true(c.convergence_deg == 0.0f);
}

static void filter_toggles(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    xrd_controls_toggle_filter(&c);
    assert_int_equal(c.filter, XRD_FILTER_BILINEAR);
    xrd_controls_toggle_filter(&c);
    assert_int_equal(c.filter, XRD_FILTER_BICUBIC);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(init_copies_start_values),
        cmocka_unit_test(zoom_steps_by_1_1),
        cmocka_unit_test(zoom_clamps_to_quarter_and_four),
        cmocka_unit_test(layout_cycles_lr_rl_mono),
        cmocka_unit_test(swap_toggles),
        cmocka_unit_test(drag_right_looks_right_drag_up_looks_up),
        cmocka_unit_test(pitch_clamps_to_89),
        cmocka_unit_test(yaw_is_not_clamped),
        cmocka_unit_test(recenter_zeroes_mouse_angles_only),
        cmocka_unit_test(init_display_defaults_and_values),
        cmocka_unit_test(convergence_steps_by_tenth),
        cmocka_unit_test(convergence_clamps_to_5),
        cmocka_unit_test(convergence_reset_returns_to_configured),
        cmocka_unit_test(filter_toggles),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
