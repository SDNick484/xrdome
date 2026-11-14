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

static void hequirect_layout_cycle(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    const xrd_layout want[] = { XRD_LAYOUT_SBS_RL, XRD_LAYOUT_TB_LR, XRD_LAYOUT_MONO,
                                XRD_LAYOUT_SBS_LR };
    for (int i = 0; i < 4; i++) {
        xrd_controls_cycle_layout(&c, 16.0 / 9.0);
        assert_int_equal(c.layout, want[i]);
    }
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_TB_RL);
    xrd_controls_cycle_layout(&c, 0.0);
    assert_int_equal(c.layout, XRD_LAYOUT_MONO);
}

static void check_flat_step(xrd_layout from, xrd_packing from_pk, double aspect,
                            xrd_layout want, xrd_packing want_pk)
{
    xrd_controls c;
    xrd_controls_init(&c, 1.5f, from);
    xrd_controls_init_format(&c, XRD_PROJ_FLAT, from_pk, 1.0f, XRD_ZOOM_SCREEN, false);
    xrd_controls_cycle_layout(&c, aspect);
    assert_int_equal(c.layout, want);
    assert_int_equal(c.packing, want_pk);
}

static void flat_layout_cycle(void **state)
{
    (void)state;
    const double a = 16.0 / 9.0;
    check_flat_step(XRD_LAYOUT_MONO, XRD_PACKING_AUTO, a, XRD_LAYOUT_SBS_LR, XRD_PACKING_HALF);
    check_flat_step(XRD_LAYOUT_SBS_LR, XRD_PACKING_HALF, a, XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL);
    check_flat_step(XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL, a, XRD_LAYOUT_TB_LR, XRD_PACKING_HALF);
    check_flat_step(XRD_LAYOUT_TB_LR, XRD_PACKING_HALF, a, XRD_LAYOUT_TB_LR, XRD_PACKING_FULL);
    check_flat_step(XRD_LAYOUT_TB_LR, XRD_PACKING_FULL, a, XRD_LAYOUT_MONO, XRD_PACKING_AUTO);
    /* an -rl layout steps to the next entry of its family position */
    check_flat_step(XRD_LAYOUT_SBS_RL, XRD_PACKING_HALF, a, XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL);
    check_flat_step(XRD_LAYOUT_SBS_RL, XRD_PACKING_FULL, a, XRD_LAYOUT_TB_LR, XRD_PACKING_HALF);
    check_flat_step(XRD_LAYOUT_TB_RL, XRD_PACKING_HALF, a, XRD_LAYOUT_TB_LR, XRD_PACKING_FULL);
    check_flat_step(XRD_LAYOUT_TB_RL, XRD_PACKING_FULL, a, XRD_LAYOUT_MONO, XRD_PACKING_AUTO);
}

static void flat_layout_cycle_from_auto_packing(void **state)
{
    (void)state;
    /* sbs AUTO resolves HALF at 16:9 and FULL at 32:9 */
    check_flat_step(XRD_LAYOUT_SBS_LR, XRD_PACKING_AUTO, 16.0 / 9.0,
                    XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL);
    check_flat_step(XRD_LAYOUT_SBS_LR, XRD_PACKING_AUTO, 32.0 / 9.0,
                    XRD_LAYOUT_TB_LR, XRD_PACKING_HALF);
    /* tb AUTO resolves HALF at 16:9 and FULL at 16:18 */
    check_flat_step(XRD_LAYOUT_TB_LR, XRD_PACKING_AUTO, 16.0 / 9.0,
                    XRD_LAYOUT_TB_LR, XRD_PACKING_FULL);
    check_flat_step(XRD_LAYOUT_TB_LR, XRD_PACKING_AUTO, 16.0 / 18.0,
                    XRD_LAYOUT_MONO, XRD_PACKING_AUTO);
    /* the 32/9 case named in the plan, from AUTO with an -rl layout */
    check_flat_step(XRD_LAYOUT_SBS_RL, XRD_PACKING_AUTO, 32.0 / 9.0,
                    XRD_LAYOUT_TB_LR, XRD_PACKING_HALF);
}

static void toggle_proj_exchanges_zooms(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.5f, XRD_LAYOUT_MONO);
    xrd_controls_init_format(&c, XRD_PROJ_FLAT, XRD_PACKING_AUTO, 1.0f, XRD_ZOOM_SCREEN, false);
    xrd_controls_toggle_proj(&c);
    assert_int_equal(c.proj, XRD_PROJ_HEQUIRECT);
    assert_float_equal(c.zoom, 1.0f, TOL);
    assert_float_equal(c.zoom_other, 1.5f, TOL);
    xrd_controls_toggle_proj(&c);
    assert_int_equal(c.proj, XRD_PROJ_FLAT);
    assert_float_equal(c.zoom, 1.5f, TOL);
    assert_float_equal(c.zoom_other, 1.0f, TOL);
}

static void zoom_keys_change_only_active_zoom(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.5f, XRD_LAYOUT_MONO);
    xrd_controls_init_format(&c, XRD_PROJ_FLAT, XRD_PACKING_AUTO, 1.0f, XRD_ZOOM_SCREEN, false);
    xrd_controls_zoom_in(&c);
    assert_float_equal(c.zoom, 1.65f, TOL);
    assert_float_equal(c.zoom_other, 1.0f, 0.0f);
    xrd_controls_zoom_out(&c);
    xrd_controls_zoom_out(&c);
    assert_float_equal(c.zoom_other, 1.0f, 0.0f);
}

static void mode_and_lock_toggles_are_flat_only(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    assert_false(xrd_controls_toggle_zoom_mode(&c));
    assert_false(xrd_controls_toggle_lock(&c));
    assert_int_equal(c.zoom_mode, XRD_ZOOM_SCREEN);
    assert_false(c.screen_locked);

    xrd_controls_init_format(&c, XRD_PROJ_FLAT, XRD_PACKING_AUTO, 1.0f, XRD_ZOOM_SCREEN, false);
    assert_true(xrd_controls_toggle_zoom_mode(&c));
    assert_int_equal(c.zoom_mode, XRD_ZOOM_MAGNIFY);
    assert_true(xrd_controls_toggle_zoom_mode(&c));
    assert_int_equal(c.zoom_mode, XRD_ZOOM_SCREEN);
    assert_true(xrd_controls_toggle_lock(&c));
    assert_true(c.screen_locked);
    assert_true(xrd_controls_toggle_lock(&c));
    assert_false(c.screen_locked);
}

static void layout_chosen_follows_cycle_and_swap(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    assert_false(c.layout_chosen);
    xrd_controls_cycle_layout(&c, 0.0);
    assert_true(c.layout_chosen);
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    assert_false(c.layout_chosen);
    xrd_controls_toggle_swap(&c);
    assert_true(c.layout_chosen);
    xrd_controls_init_format(&c, XRD_PROJ_FLAT, XRD_PACKING_AUTO, 1.0f, XRD_ZOOM_SCREEN, false);
    assert_true(c.layout_chosen); /* only init clears it */
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    assert_false(c.layout_chosen);
}

static void init_format_defaults(void **state)
{
    (void)state;
    xrd_controls c;
    xrd_controls_init(&c, 1.0f, XRD_LAYOUT_SBS_LR);
    assert_int_equal(c.proj, XRD_PROJ_HEQUIRECT);
    assert_int_equal(c.packing, XRD_PACKING_AUTO);
    assert_int_equal(c.zoom_mode, XRD_ZOOM_SCREEN);
    assert_false(c.screen_locked);
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
        cmocka_unit_test(hequirect_layout_cycle),
        cmocka_unit_test(flat_layout_cycle),
        cmocka_unit_test(flat_layout_cycle_from_auto_packing),
        cmocka_unit_test(toggle_proj_exchanges_zooms),
        cmocka_unit_test(zoom_keys_change_only_active_zoom),
        cmocka_unit_test(mode_and_lock_toggles_are_flat_only),
        cmocka_unit_test(layout_chosen_follows_cycle_and_swap),
        cmocka_unit_test(init_format_defaults),
        cmocka_unit_test(swap_toggles),
        cmocka_unit_test(drag_right_looks_right_drag_up_looks_up),
        cmocka_unit_test(pitch_clamps_to_89),
        cmocka_unit_test(yaw_is_not_clamped),
        cmocka_unit_test(recenter_zeroes_mouse_angles_only),
        cmocka_unit_test(init_display_defaults_and_values),
        cmocka_unit_test(filter_toggles),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
