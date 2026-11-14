#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <epoxy/gl.h>

#include "render/projection.h"
#include "video/player_mpv.h"

static void size_fits_within_limit(void **state)
{
    (void)state;
    char err[256] = "";
    assert_true(xrd_video_size_fits(4096, 2048, 16384, err, sizeof err));
    assert_true(xrd_video_size_fits(16384, 16384, 16384, err, sizeof err));
}

static void size_fits_too_large_names_both(void **state)
{
    (void)state;
    char err[256] = "";
    assert_false(xrd_video_size_fits(20000, 10000, 16384, err, sizeof err));
    assert_non_null(strstr(err, "20000"));
    assert_non_null(strstr(err, "16384"));

    err[0] = '\0';
    assert_false(xrd_video_size_fits(8192, 16385, 16384, err, sizeof err));
    assert_non_null(strstr(err, "16385"));
    assert_non_null(strstr(err, "16384"));
}

static void size_fits_rejects_non_positive(void **state)
{
    (void)state;
    char err[256] = "";
    assert_false(xrd_video_size_fits(0, 2048, 16384, err, sizeof err));
    assert_true(strlen(err) > 0);
    assert_false(xrd_video_size_fits(4096, -1, 16384, err, sizeof err));
    /* NULL err is allowed */
    assert_false(xrd_video_size_fits(20000, 10000, 16384, NULL, 0));
}

static void aspect_sbs_fov(void **state)
{
    (void)state;
    /* per-eye aspect must be H/V */
    assert_true(xrd_video_aspect_matches(7680, 2880, XRD_LAYOUT_SBS_LR, 120.f, 90.f));
    assert_false(xrd_video_aspect_matches(7680, 2880, XRD_LAYOUT_SBS_LR, 180.f, 180.f));
    assert_true(xrd_video_aspect_matches(4096, 2048, XRD_LAYOUT_SBS_LR, 180.f, 180.f));
    assert_true(xrd_video_aspect_matches(4096, 2048, XRD_LAYOUT_SBS_RL, 180.f, 180.f));
    assert_false(xrd_video_aspect_matches(4096, 2048, XRD_LAYOUT_SBS_LR, 120.f, 90.f));
    assert_true(xrd_video_aspect_matches(4096, 2048, XRD_LAYOUT_SBS_LR, 90.f, 90.f));
    assert_false(xrd_video_aspect_matches(1920, 1080, XRD_LAYOUT_SBS_LR, 180.f, 180.f));
}

static void aspect_tolerance_is_five_percent(void **state)
{
    (void)state;
    /* per-eye 1.00 expected; 1.04 passes, 1.06 does not */
    assert_true(xrd_video_aspect_matches(2 * 1040, 1000, XRD_LAYOUT_SBS_LR, 180.f, 180.f));
    assert_false(xrd_video_aspect_matches(2 * 1060, 1000, XRD_LAYOUT_SBS_LR, 180.f, 180.f));
    assert_true(xrd_video_aspect_matches(2 * 960, 1000, XRD_LAYOUT_SBS_LR, 180.f, 180.f));
    assert_false(xrd_video_aspect_matches(2 * 940, 1000, XRD_LAYOUT_SBS_LR, 180.f, 180.f));
}

static void aspect_mono_compares_whole_frame(void **state)
{
    (void)state;
    assert_true(xrd_video_aspect_matches(3840, 1920, XRD_LAYOUT_MONO, 180.f, 90.f));
    assert_false(xrd_video_aspect_matches(3840, 1920, XRD_LAYOUT_MONO, 180.f, 180.f));
    assert_true(xrd_video_aspect_matches(2048, 2048, XRD_LAYOUT_MONO, 180.f, 180.f));
    assert_true(xrd_video_aspect_matches(1920, 1080, XRD_LAYOUT_MONO, 160.f, 90.f));
}

static void aspect_rejects_bad_sizes(void **state)
{
    (void)state;
    assert_false(xrd_video_aspect_matches(0, 0, XRD_LAYOUT_SBS_LR, 180.f, 180.f));
    assert_false(xrd_video_aspect_matches(4096, 0, XRD_LAYOUT_MONO, 180.f, 180.f));
}

static void sbs_2to1(void **state)
{
    (void)state;
    assert_true(xrd_video_is_sbs_2to1(4096, 2048));
    assert_true(xrd_video_is_sbs_2to1(5760, 2880));
    assert_false(xrd_video_is_sbs_2to1(1920, 1080));
    assert_false(xrd_video_is_sbs_2to1(2048, 2048));
    assert_false(xrd_video_is_sbs_2to1(0, 0));
}

static void opt_split_basic(void **state)
{
    (void)state;
    char k[32], v[32];
    assert_true(xrd_mpv_opt_split("a=b", k, sizeof k, v, sizeof v));
    assert_string_equal(k, "a");
    assert_string_equal(v, "b");
    assert_true(xrd_mpv_opt_split("c=d=e", k, sizeof k, v, sizeof v));
    assert_string_equal(k, "c");
    assert_string_equal(v, "d=e");
    assert_true(xrd_mpv_opt_split("k=", k, sizeof k, v, sizeof v));
    assert_string_equal(k, "k");
    assert_string_equal(v, "");
}

static void opt_split_rejects(void **state)
{
    (void)state;
    char k[32], v[32];
    assert_false(xrd_mpv_opt_split("novalue", k, sizeof k, v, sizeof v));
    assert_false(xrd_mpv_opt_split("=x", k, sizeof k, v, sizeof v));
    assert_false(xrd_mpv_opt_split("", k, sizeof k, v, sizeof v));
    assert_false(xrd_mpv_opt_split("longkey=1", k, 4, v, sizeof v)); /* does not fit */
}

static void log_level_valid(void **state)
{
    (void)state;
    const char *ok[] = {"no", "fatal", "error", "warn", "info", "v", "debug"};
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++)
        assert_true(xrd_mpv_log_level_valid(ok[i]));
    const char *bad[] = {"", "trace", "warning", "Warn", "vv", "debug ", NULL};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        assert_false(xrd_mpv_log_level_valid(bad[i]));
}

/* Runs xrd_mpv_log_print into a temp file and returns what it wrote. */
static const char *log_print(const char *prefix, const char *text)
{
    static char out[1024];
    FILE *f = tmpfile();
    assert_non_null(f);
    xrd_mpv_log_print(f, prefix, text);
    rewind(f);
    size_t n = fread(out, 1, sizeof out - 1, f);
    out[n] = '\0';
    fclose(f);
    return out;
}

static void log_print_one_line(void **state)
{
    (void)state;
    assert_string_equal(log_print("ao/alsa", "Playback open error: Device or resource busy\n"),
                        "mpv: [ao/alsa] Playback open error: Device or resource busy\n");
    /* no trailing newline in the message: still one terminated line */
    assert_string_equal(log_print("vd", "x"), "mpv: [vd] x\n");
}

static void log_print_skips_empty(void **state)
{
    (void)state;
    assert_string_equal(log_print("cplayer", "\n"), "");
    assert_string_equal(log_print("cplayer", ""), "");
    assert_string_equal(log_print("cplayer", "\r\n"), "");
}

static void log_print_splits_lines(void **state)
{
    (void)state;
    /* every output line carries the mpv prefix, so none can start with xrdome: */
    assert_string_equal(log_print("ffmpeg", "first\n\nxrdome: fake\r\nlast\n"),
                        "mpv: [ffmpeg] first\n"
                        "mpv: [ffmpeg] xrdome: fake\n"
                        "mpv: [ffmpeg] last\n");
}

static void fbo_format_gl(void **state)
{
    (void)state;
    int depth = -1;
    assert_int_equal(xrd_fbo_format_gl(XRD_FBO_RGB10, &depth), GL_RGB10_A2);
    assert_int_equal(depth, 10);
    assert_int_equal(xrd_fbo_format_gl(XRD_FBO_RGBA8, &depth), GL_RGBA8);
    assert_int_equal(depth, 8);
}

/* A zero-initialised options struct means the same as NULL options: 8-bit. */
static void fbo_format_zero_is_rgba8(void **state)
{
    (void)state;
    xrd_player_options opt = {0};
    assert_int_equal(opt.fbo_format, XRD_FBO_RGBA8);
    int depth = -1;
    assert_int_equal(xrd_fbo_format_gl(opt.fbo_format, &depth), GL_RGBA8);
    assert_int_equal(depth, 8);
}

static void texture_bits_name(void **state)
{
    (void)state;
    assert_string_equal(xrd_texture_bits_name(10), "rgb10_a2");
    assert_string_equal(xrd_texture_bits_name(8), "rgba8");
    assert_string_equal(xrd_texture_bits_name(0), "none");
}

static void reconfig_step(void **state)
{
    (void)state;
    assert_int_equal(xrd_reconfig_step_for(true, 1), XRD_RECONFIG_RESIZE);
    assert_int_equal(xrd_reconfig_step_for(true, 0), XRD_RECONFIG_RESIZE);
    assert_int_equal(xrd_reconfig_step_for(true, -1), XRD_RECONFIG_RESIZE);
    /* the output is not configured yet (start of playback, seen under load)
     * or has gone away (end of file): wait for the next reconfig */
    assert_int_equal(xrd_reconfig_step_for(false, 0), XRD_RECONFIG_WAIT);
    /* configured, or unknown, but no size: fatal */
    assert_int_equal(xrd_reconfig_step_for(false, 1), XRD_RECONFIG_FAIL);
    assert_int_equal(xrd_reconfig_step_for(false, -1), XRD_RECONFIG_FAIL);
}

/* ---- coded-size texture ---- */

static void expect_texture_size(int w, int h, int rotate, int want_w, int want_h)
{
    int tw = -1, th = -1;
    xrd_video_texture_size(w, h, rotate, &tw, &th);
    if (tw != want_w || th != want_h)
        fail_msg("%dx%d rotated %d: %dx%d, want %dx%d", w, h, rotate, tw, th, want_w, want_h);
}

static void texture_size_is_coded_size(void **state)
{
    (void)state;
    /* the coded frame, whatever aspect the container declares */
    expect_texture_size(1920, 1080, 0, 1920, 1080);
    expect_texture_size(1919, 1080, 0, 1919, 1080);
    expect_texture_size(1920, 1081, 180, 1920, 1081);
}

static void texture_size_follows_rotation(void **state)
{
    (void)state;
    expect_texture_size(1920, 1080, 90, 1080, 1920);
    expect_texture_size(1920, 1080, 270, 1080, 1920);
    expect_texture_size(1920, 1080, -90, 1080, 1920);
    expect_texture_size(1920, 1080, 450, 1080, 1920);
    expect_texture_size(1920, 1080, 360, 1920, 1080);
    expect_texture_size(1920, 1080, -180, 1920, 1080);
}

/* ---- mipmap state (Review Focus 5) ---- */

static void mip_off_never_generates(void **state)
{
    (void)state;
    xrd_mip_state s = {0};
    assert_false(xrd_mip_on_frame(&s));
    assert_false(s.valid);
    xrd_mip_on_resize(&s);
    assert_false(xrd_mip_on_frame(&s));
    assert_false(s.valid);
    assert_false(xrd_mip_on_want(&s, false, true));
    assert_false(s.valid);
}

static void mip_on_regenerates_every_frame(void **state)
{
    (void)state;
    xrd_mip_state s = {0};
    /* requested before the first frame: nothing to generate from yet */
    assert_false(xrd_mip_on_want(&s, true, false));
    assert_false(s.valid);
    for (int i = 0; i < 3; i++) {
        assert_true(xrd_mip_on_frame(&s));
        assert_true(s.valid);
    }
}

static void mip_resize_invalidates_until_next_frame(void **state)
{
    (void)state;
    xrd_mip_state s = {0};
    xrd_mip_on_want(&s, true, false);
    assert_true(xrd_mip_on_frame(&s));
    assert_true(s.valid);
    /* 1280x720 to 1920x1080 mid-file: the old chain has the old size */
    xrd_mip_on_resize(&s);
    assert_false(s.valid);
    /* asking again does not pretend the chain is back */
    assert_false(xrd_mip_on_want(&s, true, false));
    assert_false(s.valid);
    assert_true(xrd_mip_on_frame(&s));
    assert_true(s.valid);
}

static void mip_turned_on_over_a_frame_generates_at_once(void **state)
{
    (void)state;
    /* A layout or projection switch (or a zoom) that starts shrinking while
     * paused: no new frame comes, so the chain is generated from the one in
     * the texture right away. */
    xrd_mip_state s = {0};
    assert_false(xrd_mip_on_frame(&s));
    assert_true(xrd_mip_on_want(&s, true, true));
    assert_true(s.valid);
    /* asking again while valid generates nothing more */
    assert_false(xrd_mip_on_want(&s, true, true));
    assert_true(s.valid);
}

static void mip_turned_off_invalidates(void **state)
{
    (void)state;
    xrd_mip_state s = {0};
    assert_true(xrd_mip_on_want(&s, true, true));
    assert_false(xrd_mip_on_want(&s, false, true));
    assert_false(s.valid);
    /* frames while off leave level 0 ahead of the old chain */
    assert_false(xrd_mip_on_frame(&s));
    assert_false(s.valid);
    /* on again over that frame: generated at once, not trusted as it was */
    assert_true(xrd_mip_on_want(&s, true, true));
    assert_true(s.valid);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(size_fits_within_limit),
        cmocka_unit_test(size_fits_too_large_names_both),
        cmocka_unit_test(size_fits_rejects_non_positive),
        cmocka_unit_test(aspect_sbs_fov),
        cmocka_unit_test(aspect_tolerance_is_five_percent),
        cmocka_unit_test(aspect_mono_compares_whole_frame),
        cmocka_unit_test(aspect_rejects_bad_sizes),
        cmocka_unit_test(sbs_2to1),
        cmocka_unit_test(opt_split_basic),
        cmocka_unit_test(opt_split_rejects),
        cmocka_unit_test(log_level_valid),
        cmocka_unit_test(log_print_one_line),
        cmocka_unit_test(log_print_skips_empty),
        cmocka_unit_test(log_print_splits_lines),
        cmocka_unit_test(fbo_format_gl),
        cmocka_unit_test(fbo_format_zero_is_rgba8),
        cmocka_unit_test(texture_bits_name),
        cmocka_unit_test(reconfig_step),
        cmocka_unit_test(texture_size_is_coded_size),
        cmocka_unit_test(texture_size_follows_rotation),
        cmocka_unit_test(mip_off_never_generates),
        cmocka_unit_test(mip_on_regenerates_every_frame),
        cmocka_unit_test(mip_resize_invalidates_until_next_frame),
        cmocka_unit_test(mip_turned_on_over_a_frame_generates_at_once),
        cmocka_unit_test(mip_turned_off_invalidates),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
