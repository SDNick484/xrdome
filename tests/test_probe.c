#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "detect/probe.h"

#ifndef XRD_FIXTURE_DIR
#error "XRD_FIXTURE_DIR must be defined by the build"
#endif

static char *fixture(const char *name)
{
    static char buf[1 << 16];
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", XRD_FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        fail_msg("cannot open fixture %s", path);
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static void owner_files(void **s)
{
    (void)s;
    xrd_probe p;
    assert_true(xrd_probe_parse(fixture("owner_vr180_mkv.txt"), &p));
    assert_true(p.have_size); assert_int_equal(p.width, 2880); assert_int_equal(p.height, 1440);
    assert_int_equal(p.sar_num, 1); assert_int_equal(p.sar_den, 1);
    assert_int_equal(p.stereo, XRD_STEREO_NONE); assert_int_equal(p.spherical, XRD_SPH_NONE);
    assert_true(xrd_probe_parse(fixture("owner_vr180_mp4.txt"), &p));
    assert_int_equal(p.width, 4320); assert_int_equal(p.height, 2160);
    assert_int_equal(p.stereo, XRD_STEREO_NONE); assert_int_equal(p.spherical, XRD_SPH_NONE);
}

static void stereo_files(void **s)
{
    (void)s;
    xrd_probe p;
    assert_true(xrd_probe_parse(fixture("sbs_mkv.txt"), &p));
    assert_int_equal(p.stereo, XRD_STEREO_SBS); assert_false(p.stereo_inverted);
    assert_int_equal(p.width, 1920); assert_int_equal(p.height, 1080);
    assert_true(xrd_probe_parse(fixture("sbs_inverted_mkv.txt"), &p));
    assert_int_equal(p.stereo, XRD_STEREO_SBS); assert_true(p.stereo_inverted);
    assert_true(xrd_probe_parse(fixture("tb_mkv.txt"), &p));
    assert_int_equal(p.stereo, XRD_STEREO_TB); assert_false(p.stereo_inverted);
    assert_true(xrd_probe_parse(fixture("sbs_mp4.txt"), &p));
    assert_int_equal(p.stereo, XRD_STEREO_SBS);
}

static void stereo_unspecified_is_other(void **s)
{
    (void)s;
    /* What FFmpeg 9.0.2 prints for an MP4 that carries a vexu box. */
    xrd_probe p;
    assert_true(xrd_probe_parse(fixture("sbs_mp4_vexu.txt"), &p));
    assert_int_equal(p.stereo, XRD_STEREO_OTHER);
    assert_string_equal(p.stereo_name, "unspecified");
}

static void spherical_files(void **s)
{
    (void)s;
    xrd_probe p;
    assert_true(xrd_probe_parse(fixture("spherical_mkv.txt"), &p));
    assert_int_equal(p.spherical, XRD_SPH_EQUIRECT);
    assert_string_equal(p.spherical_name, "equirectangular");
    assert_int_equal(p.stereo, XRD_STEREO_NONE);
    assert_true(xrd_probe_parse(fixture("spherical_mp4.txt"), &p));
    assert_int_equal(p.spherical, XRD_SPH_EQUIRECT);
    assert_true(xrd_probe_parse(fixture("spherical_cubemap_mp4.txt"), &p));
    assert_int_equal(p.spherical, XRD_SPH_OTHER);
    assert_string_equal(p.spherical_name, "cubemap");
}

static void coverart_first(void **s)
{
    (void)s;
    xrd_probe p;
    assert_true(xrd_probe_parse(fixture("coverart_first.txt"), &p));
    assert_int_equal(p.width, 1280); assert_int_equal(p.height, 720);
}

static void no_video(void **s)
{
    (void)s;
    xrd_probe p;
    assert_false(xrd_probe_parse(fixture("audio_only.txt"), &p));
    assert_false(p.have_size); assert_int_equal(p.sar_num, 1); assert_int_equal(p.sar_den, 1);
    assert_false(xrd_probe_parse(fixture("empty.txt"), &p));
    assert_false(xrd_probe_parse(NULL, &p));
    assert_int_equal(p.stereo, XRD_STEREO_NONE);
}

static void truncated(void **s)
{
    (void)s;
    xrd_probe p;
    /* Cut mid-block: no closing [/STREAM], so no half-parsed stream is returned. */
    assert_false(xrd_probe_parse(fixture("truncated.txt"), &p));
    assert_false(p.have_size);
    assert_false(xrd_probe_parse("[STREAM]\ncodec_type=vid", &p));
    assert_false(xrd_probe_parse("[STREAM]\n[SIDE_DATA]\nside_data_type=Stereo 3D\ntype=side by", &p));
}

static void junk_values(void **s)
{
    (void)s;
    xrd_probe p;
    const char *zero = "[STREAM]\ncodec_type=video\nwidth=0\nheight=0\n"
                       "sample_aspect_ratio=0:1\nDISPOSITION:attached_pic=0\n[/STREAM]\n";
    assert_true(xrd_probe_parse(zero, &p));
    assert_false(p.have_size);
    assert_int_equal(p.sar_num, 1); assert_int_equal(p.sar_den, 1);
    const char *na = "[STREAM]\ncodec_type=video\nwidth=N/A\nheight=1080\n"
                     "sample_aspect_ratio=N/A\n[/STREAM]\n";
    assert_true(xrd_probe_parse(na, &p));
    assert_false(p.have_size);
    assert_int_equal(p.sar_num, 1); assert_int_equal(p.sar_den, 1);
    const char *neg = "[STREAM]\ncodec_type=video\nwidth=1920\nheight=-5\n"
                      "sample_aspect_ratio=4:-3\n[/STREAM]\n";
    assert_true(xrd_probe_parse(neg, &p));
    assert_false(p.have_size);
    assert_int_equal(p.sar_num, 1); assert_int_equal(p.sar_den, 1);
    const char *anamorphic = "[STREAM]\ncodec_type=video\nwidth=720\nheight=576\n"
                             "sample_aspect_ratio=16:15\n[/STREAM]\n";
    assert_true(xrd_probe_parse(anamorphic, &p));
    assert_true(p.have_size);
    assert_int_equal(p.sar_num, 16); assert_int_equal(p.sar_den, 15);
}

static void unknown_stereo_type(void **s)
{
    (void)s;
    xrd_probe p;
    const char *t = "[STREAM]\ncodec_type=video\nwidth=1920\nheight=1080\n"
                    "[SIDE_DATA]\nside_data_type=Stereo 3D\ntype=frame alternate\ninverted=0\n"
                    "[/SIDE_DATA]\n[/STREAM]\n";
    assert_true(xrd_probe_parse(t, &p));
    assert_int_equal(p.stereo, XRD_STEREO_OTHER);
    assert_string_equal(p.stereo_name, "frame alternate");
    const char *two_d = "[STREAM]\ncodec_type=video\nwidth=1920\nheight=1080\n"
                        "[SIDE_DATA]\nside_data_type=Stereo 3D\ntype=2D\n[/SIDE_DATA]\n[/STREAM]\n";
    assert_true(xrd_probe_parse(two_d, &p));
    assert_int_equal(p.stereo, XRD_STEREO_2D);
}

static void other_side_data_ignored(void **s)
{
    (void)s;
    xrd_probe p;
    const char *t = "[STREAM]\ncodec_type=video\nwidth=1920\nheight=1080\n"
                    "[SIDE_DATA]\nside_data_type=Display Matrix\ntype=side by side\n"
                    "projection=equirectangular\n[/SIDE_DATA]\n[/STREAM]\n";
    assert_true(xrd_probe_parse(t, &p));
    assert_int_equal(p.stereo, XRD_STEREO_NONE);
    assert_int_equal(p.spherical, XRD_SPH_NONE);
}

static void first_video_stream_wins(void **s)
{
    (void)s;
    xrd_probe p;
    const char *t = "[STREAM]\ncodec_type=video\nwidth=640\nheight=360\n[/STREAM]\n"
                    "[STREAM]\ncodec_type=video\nwidth=1920\nheight=1080\n[/STREAM]\n";
    assert_true(xrd_probe_parse(t, &p));
    assert_int_equal(p.width, 640);
    /* CRLF line ends do not matter. */
    const char *crlf = "[STREAM]\r\ncodec_type=video\r\nwidth=320\r\nheight=240\r\n[/STREAM]\r\n";
    assert_true(xrd_probe_parse(crlf, &p));
    assert_int_equal(p.width, 320); assert_int_equal(p.height, 240);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(owner_files),
        cmocka_unit_test(stereo_files),
        cmocka_unit_test(stereo_unspecified_is_other),
        cmocka_unit_test(spherical_files),
        cmocka_unit_test(coverart_first),
        cmocka_unit_test(no_video),
        cmocka_unit_test(truncated),
        cmocka_unit_test(junk_values),
        cmocka_unit_test(unknown_stereo_type),
        cmocka_unit_test(other_side_data_ignored),
        cmocka_unit_test(first_video_stream_wins),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
