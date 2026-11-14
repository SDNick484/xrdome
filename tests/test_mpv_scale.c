#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <cmocka.h>

#include "render/projection.h"
#include "video/mpv_scale.h"

#define MS 1000000ull

/* The flat geometry for a eye_w x eye_h viewport at the Viture Pro's 43
 * degrees, screen zoom mode. */
static xrd_view_geom flat_geom(int eye_w, int eye_h, float zoom, double picture_aspect)
{
    xrd_view_geom g;
    xrd_view_geometry(XRD_PROJ_FLAT, XRD_ZOOM_SCREEN, 43.f, (float)eye_w / (float)eye_h, zoom,
                      picture_aspect, &g);
    return g;
}

/* A geometry with exact numbers: tan 0.5 on both axes over a 100x100 eye,
 * so half_w = 0.6 is 120 display pixels. */
static xrd_view_geom exact_geom(float half_w, float half_h)
{
    xrd_view_geom g = {.tan_h = 0.5f, .tan_v = 0.5f, .half_w = half_w, .half_h = half_h};
    return g;
}

static void size_mono_1080p_at_zoom_1_5(void **state)
{
    (void)state;
    /* The owner's case: a 1080p film at the default flat zoom on a 1920x1080 eye. */
    xrd_view_geom g = flat_geom(1920, 1080, 1.5f, 16.0 / 9.0);
    int w, h;
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 1920, 1080, 1920, 1080, 16384, &w, &h));
    assert_int_equal(w, 2880);
    assert_int_equal(h, 1620);
}

static void size_smoke_window(void **state)
{
    (void)state;
    /* The smoke test's 640x360 eye: a 480x270 picture at zoom 1.5 and 1.0. */
    xrd_view_geom g = flat_geom(640, 360, 1.5f, 16.0 / 9.0);
    int w, h;
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 480, 270, 640, 360, 16384, &w, &h));
    assert_int_equal(w, 960);
    assert_int_equal(h, 540);
    g = flat_geom(640, 360, 1.0f, 16.0 / 9.0);
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 480, 270, 640, 360, 16384, &w, &h));
    assert_int_equal(w, 640);
    assert_int_equal(h, 360);
}

static void size_keeps_side_by_side(void **state)
{
    (void)state;
    int w, h;
    /* Half SBS 480x270: two 240x270 eyes, each a 16:9 picture filling a 640x360
     * eye at zoom 1, so two 640x360 eyes side by side. */
    xrd_view_geom g = flat_geom(640, 360, 1.0f,
                                xrd_picture_aspect(XRD_LAYOUT_SBS_LR, XRD_PACKING_HALF, 480.0 / 270.0));
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_LR, 480, 270, 640, 360, 16384, &w, &h));
    assert_int_equal(w, 1280);
    assert_int_equal(h, 360);
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_RL, 480, 270, 640, 360, 16384, &w, &h));
    assert_int_equal(w, 1280);
    assert_int_equal(h, 360);

    /* Full SBS 960x270: two 480x270 eyes with the same 16:9 picture. */
    g = flat_geom(640, 360, 1.0f,
                  xrd_picture_aspect(XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL, 960.0 / 270.0));
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_LR, 960, 270, 640, 360, 16384, &w, &h));
    assert_int_equal(w, 1280);
    assert_int_equal(h, 360);

    /* The same 480x270 frame taken as full SBS: each eye is an 8:9 picture,
     * 320 display pixels wide at the full height. */
    g = flat_geom(640, 360, 1.0f,
                  xrd_picture_aspect(XRD_LAYOUT_SBS_LR, XRD_PACKING_FULL, 480.0 / 270.0));
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_LR, 480, 270, 640, 360, 16384, &w, &h));
    assert_int_equal(w, 640);
    assert_int_equal(h, 360);
}

static void size_keeps_top_bottom(void **state)
{
    (void)state;
    int w, h;
    /* Full top-bottom 480x540: two 480x270 eyes, stacked. */
    xrd_view_geom g = flat_geom(640, 360, 1.0f,
                                xrd_picture_aspect(XRD_LAYOUT_TB_LR, XRD_PACKING_FULL, 480.0 / 540.0));
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_TB_LR, 480, 540, 640, 360, 16384, &w, &h));
    assert_int_equal(w, 640);
    assert_int_equal(h, 720);
    /* Half top-bottom 480x270: two 480x135 eyes, each a 16:9 picture. */
    g = flat_geom(640, 360, 1.0f,
                  xrd_picture_aspect(XRD_LAYOUT_TB_RL, XRD_PACKING_HALF, 480.0 / 270.0));
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_TB_RL, 480, 270, 640, 360, 16384, &w, &h));
    assert_int_equal(w, 640);
    assert_int_equal(h, 720);
}

static void size_odd_sizes(void **state)
{
    (void)state;
    int w, h;
    /* 120.3 x 120.5 display pixels round to 120 x 121; a mono texture may be odd. */
    xrd_view_geom g = exact_geom(0.6015f, 0.6025f);
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 99, 99, 100, 100, 0, &w, &h));
    assert_int_equal(w, 120);
    assert_int_equal(h, 121);
    /* An odd split axis: the eye is the whole texels below half (49 of 99),
     * and the enlarged texture is two whole eyes, so even on that axis. */
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_LR, 99, 99, 100, 100, 0, &w, &h));
    assert_int_equal(w, 240);
    assert_int_equal(h, 121);
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_TB_LR, 99, 99, 100, 100, 0, &w, &h));
    assert_int_equal(w, 120);
    assert_int_equal(h, 242);
}

static void size_cap_at_texel_budget(void **state)
{
    (void)state;
    int w, h;
    /* 1080p at zoom 4: 7680x4320 wanted, four times the budget, so half of it
     * on each axis. */
    xrd_view_geom g = exact_geom(30.f, 16.875f); /* x 256 over a 128x128 eye */
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 1920, 1080, 128, 128, 16384, &w, &h));
    assert_int_equal(w, 3840);
    assert_int_equal(h, 2160);
    /* Half SBS 1920x1080 at zoom 1.5 on the glasses: two 2880x1620 eyes are
     * 9.3 Mtexels; capped below 8.3 with the shape kept, still enlarged. */
    g = flat_geom(1920, 1080, 1.5f,
                  xrd_picture_aspect(XRD_LAYOUT_SBS_LR, XRD_PACKING_HALF, 1920.0 / 1080.0));
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_LR, 1920, 1080, 1920, 1080, 16384, &w, &h));
    assert_true((long)w * h <= XRD_MPV_SCALE_MAX_TEXELS);
    assert_true((long)w * h > XRD_MPV_SCALE_MAX_TEXELS - 2 * (w + h));
    assert_int_equal(w % 2, 0);
    assert_true(fabs((double)w / 2 / h - 2880.0 / 1620.0) < 0.002);
    assert_true(w / 2 > 960 && h > 1080);
}

static void size_cap_at_max_texture(void **state)
{
    (void)state;
    int w, h;
    xrd_view_geom g = flat_geom(1920, 1080, 1.5f, 16.0 / 9.0); /* wants 2880x1620 */
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 1920, 1080, 1920, 1080, 2048, &w, &h));
    assert_int_equal(w, 2048);
    assert_int_equal(h, 1152);
    /* Side by side, the limit is on the whole texture: two 1024-wide eyes,
     * each still larger than the coded 480x540 eye. */
    assert_true(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_LR, 960, 540, 1920, 1080, 2048, &w, &h));
    assert_int_equal(w, 2048);
    assert_int_equal(h, 576);
    /* With a 1080-high eye the same cap leaves 576 rows: not enlarged. */
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_LR, 960, 1080, 1920, 1080, 2048, &w, &h));
    assert_int_equal(w, 960);
    assert_int_equal(h, 1080);
    /* A cap that leaves an axis at or below the coded size: today's path. */
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 1920, 1080, 1920, 1080, 1920, &w, &h));
    assert_int_equal(w, 1920);
    assert_int_equal(h, 1080);
}

static void size_not_enlarged_keeps_coded(void **state)
{
    (void)state;
    int w = 0, h = 0;
    /* 1:1 (zoom 1 on the glasses), shrunk (4K at zoom 1.5), and one axis
     * enlarged with the other shrunk: all today's path, coded size. */
    xrd_view_geom g = flat_geom(1920, 1080, 1.0f, 16.0 / 9.0);
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 1920, 1080, 1920, 1080, 16384, &w, &h));
    assert_int_equal(w, 1920);
    assert_int_equal(h, 1080);
    g = flat_geom(1920, 1080, 1.5f, 16.0 / 9.0);
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 3840, 2160, 1920, 1080, 16384, &w, &h));
    assert_int_equal(w, 3840);
    assert_int_equal(h, 2160);
    g = exact_geom(0.6f, 0.6f); /* 120 x 120 display pixels */
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 100, 130, 100, 100, 0, &w, &h));
    assert_int_equal(w, 100);
    assert_int_equal(h, 130);
}

static void size_rejects_vr180_and_bad_input(void **state)
{
    (void)state;
    int w = -1, h = -1;
    xrd_view_geom vr;
    xrd_view_geometry(XRD_PROJ_HEQUIRECT, XRD_ZOOM_SCREEN, 43.f, 16.f / 9.f, 1.f, 16.0 / 9.0, &vr);
    assert_false(xrd_mpv_scale_size(&vr, XRD_LAYOUT_SBS_LR, 480, 240, 1920, 1080, 16384, &w, &h));
    assert_int_equal(w, 480);
    assert_int_equal(h, 240);

    xrd_view_geom g = exact_geom(0.6f, 0.6f);
    assert_false(xrd_mpv_scale_size(NULL, XRD_LAYOUT_MONO, 50, 50, 100, 100, 0, &w, &h));
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 0, 50, 100, 100, 0, &w, &h));
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 50, 50, 100, -1, 0, &w, &h));
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_SBS_LR, 1, 50, 100, 100, 0, &w, &h));
    g.half_w = NAN;
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 50, 50, 100, 100, 0, &w, &h));
    g = exact_geom(0.6f, 0.6f);
    g.tan_v = INFINITY;
    assert_false(xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, 50, 50, 100, 100, 0, &w, &h));
}

/* Below the cap the size agrees with xrd_minify_ratio: true exactly when
 * the ratio is below 1, and then the eye maps 1:1 at the centre. */
static void size_matches_minify_ratio(void **state)
{
    (void)state;
    const float zooms[] = {0.5f, 0.9f, 1.0f, 1.1f, 1.5f, 2.0f};
    const int sizes[][2] = {{1920, 1080}, {1280, 720}, {720, 480}, {960, 1080}};
    for (size_t z = 0; z < sizeof zooms / sizeof zooms[0]; z++) {
        for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            int tw = sizes[s][0], th = sizes[s][1];
            xrd_view_geom g = flat_geom(1920, 1080, zooms[z], (double)tw / th);
            float r = xrd_minify_ratio(XRD_PROJ_FLAT, &g, tw, th, 1920, 1080, 180.f, 180.f);
            int w, h;
            bool on = xrd_mpv_scale_size(&g, XRD_LAYOUT_MONO, tw, th, 1920, 1080, 0, &w, &h);
            if ((long)w * h >= XRD_MPV_SCALE_MAX_TEXELS - 4L * (w + h))
                continue; /* capped: checked above */
            /* 1:1 within rounding counts as either */
            if (fabsf(r - 1.f) < 1e-3f)
                continue;
            assert_int_equal(on, r < 1.f);
            if (on) {
                float r1 = xrd_minify_ratio(XRD_PROJ_FLAT, &g, w, h, 1920, 1080, 180.f, 180.f);
                assert_true(fabsf(r1 - 1.f) < 1.5f / (float)(h < w ? h : w));
            }
        }
    }
}

static void debounce_first_size_applies_at_once(void **state)
{
    (void)state;
    xrd_size_debounce d = {0};
    assert_true(xrd_size_debounce_step(&d, 960, 540, 5 * MS, 250 * MS));
    assert_int_equal(d.w, 960);
    assert_int_equal(d.h, 540);
    /* a first size of 0 x 0 (the coded size) changes nothing */
    xrd_size_debounce e = {0};
    assert_false(xrd_size_debounce_step(&e, 0, 0, 5 * MS, 250 * MS));
    assert_int_equal(e.w, 0);
    assert_true(e.started);
}

static void debounce_waits_for_a_settled_size(void **state)
{
    (void)state;
    xrd_size_debounce d = {0};
    xrd_size_debounce_step(&d, 960, 540, 0, 250 * MS);
    assert_false(xrd_size_debounce_step(&d, 1056, 594, 1000 * MS, 250 * MS));
    assert_false(xrd_size_debounce_step(&d, 1056, 594, 1249 * MS, 250 * MS));
    assert_int_equal(d.w, 960);
    assert_true(xrd_size_debounce_step(&d, 1056, 594, 1250 * MS, 250 * MS));
    assert_int_equal(d.w, 1056);
    assert_int_equal(d.h, 594);
    assert_false(xrd_size_debounce_step(&d, 1056, 594, 1300 * MS, 250 * MS));
}

static void debounce_held_zoom_key_reallocates_once(void **state)
{
    (void)state;
    /* `]` held: a new size every 33 ms for 10 steps, then 60 Hz frames with
     * the last one. Nothing changes until it has held for 250 ms. */
    xrd_size_debounce d = {0};
    xrd_size_debounce_step(&d, 960, 540, 0, 250 * MS);
    int changes = 0, w = 960;
    uint64_t t = 1000 * MS;
    for (int i = 0; i < 10; i++, t += 33 * MS) {
        w += 96;
        changes += xrd_size_debounce_step(&d, w, w * 9 / 16, t, 250 * MS);
    }
    uint64_t last = t - 33 * MS;
    for (; t < last + 400 * MS; t += 16 * MS) {
        bool c = xrd_size_debounce_step(&d, w, w * 9 / 16, t, 250 * MS);
        if (c)
            assert_true(t >= last + 250 * MS);
        changes += c;
    }
    assert_int_equal(changes, 1);
    assert_int_equal(d.w, 1920);
    assert_int_equal(d.h, 1080);
}

static void debounce_return_to_current_cancels(void **state)
{
    (void)state;
    xrd_size_debounce d = {0};
    xrd_size_debounce_step(&d, 960, 540, 0, 250 * MS);
    assert_false(xrd_size_debounce_step(&d, 0, 0, 100 * MS, 250 * MS));
    assert_false(xrd_size_debounce_step(&d, 960, 540, 200 * MS, 250 * MS));
    assert_false(d.pending);
    /* the wait for 0 x 0 starts again from here, not from 100 ms */
    assert_false(xrd_size_debounce_step(&d, 0, 0, 400 * MS, 250 * MS));
    assert_false(xrd_size_debounce_step(&d, 0, 0, 600 * MS, 250 * MS));
    assert_true(xrd_size_debounce_step(&d, 0, 0, 650 * MS, 250 * MS));
    assert_int_equal(d.w, 0);
    assert_int_equal(d.h, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(size_mono_1080p_at_zoom_1_5),
        cmocka_unit_test(size_smoke_window),
        cmocka_unit_test(size_keeps_side_by_side),
        cmocka_unit_test(size_keeps_top_bottom),
        cmocka_unit_test(size_odd_sizes),
        cmocka_unit_test(size_cap_at_texel_budget),
        cmocka_unit_test(size_cap_at_max_texture),
        cmocka_unit_test(size_not_enlarged_keeps_coded),
        cmocka_unit_test(size_rejects_vr180_and_bad_input),
        cmocka_unit_test(size_matches_minify_ratio),
        cmocka_unit_test(debounce_first_size_applies_at_once),
        cmocka_unit_test(debounce_waits_for_a_settled_size),
        cmocka_unit_test(debounce_held_zoom_key_reallocates_once),
        cmocka_unit_test(debounce_return_to_current_cancels),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
