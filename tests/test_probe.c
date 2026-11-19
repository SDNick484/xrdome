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

static void image_formats(void **s)
{
    (void)s;
    xrd_probe p;
    assert_true(xrd_probe_parse(fixture("image_jpeg.txt"), &p));
    assert_true(p.image);
    /* The stored size: ffprobe does not report the EXIF turn mpv applies. */
    assert_int_equal(p.width, 96); assert_int_equal(p.height, 48);
    assert_true(xrd_probe_parse(fixture("image_png.txt"), &p));
    assert_true(p.image);
    assert_true(xrd_probe_parse(fixture("image_gif.txt"), &p));
    assert_false(p.image);
    assert_true(xrd_probe_parse(fixture("video_mp4_format.txt"), &p));
    assert_false(p.image);
    assert_int_equal(p.width, 64); assert_int_equal(p.height, 48);
    /* No [FORMAT] block (-show_streams only), or an unfinished one: not an image. */
    assert_true(xrd_probe_parse(fixture("sbs_mkv.txt"), &p));
    assert_false(p.image);
    assert_true(xrd_probe_parse(fixture("image_format_truncated.txt"), &p));
    assert_false(p.image);
    /* A format block with no usable stream does not make one. */
    assert_false(xrd_probe_parse("[FORMAT]\nformat_name=image2\n[/FORMAT]\n", &p));
    assert_false(p.image);
}

static void image_format_names(void **s)
{
    (void)s;
    const char *yes[] = {"image2", "image2pipe", "jpeg_pipe", "png_pipe", "webp_pipe",
                         "tiff_pipe", "bmp_pipe"};
    const char *no[] = {"", "_pipe", "gif", "apng", "mov,mp4,m4a,3gp,3g2,mj2", "matroska,webm",
                        "mpegts", "image", "pipe"};
    for (size_t i = 0; i < sizeof yes / sizeof yes[0]; i++)
        assert_true(xrd_probe_format_is_image(yes[i]));
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++)
        assert_false(xrd_probe_format_is_image(no[i]));
    assert_false(xrd_probe_format_is_image(NULL));
}

/* ---- Windows command-line quoting ---------------------------------------- */

static const char *quote1(const char *arg)
{
    static char buf[512];
    size_t n = 0;
    buf[0] = '\0';
    assert_true(xrd_win_quote_arg(arg, buf, sizeof buf, &n));
    assert_int_equal(n, strlen(buf));
    return buf;
}

static void win_quote_arg_cases(void **state)
{
    (void)state;
    assert_string_equal(quote1("abc"), "abc");
    assert_string_equal(quote1("-show_streams"), "-show_streams");
    assert_string_equal(quote1(""), "\"\"");
    assert_string_equal(quote1("a b"), "\"a b\"");
    assert_string_equal(quote1("a\tb"), "\"a\tb\"");
    assert_string_equal(quote1("a\nb"), "\"a\nb\"");
    assert_string_equal(quote1("a\vb"), "\"a\vb\"");
    /* backslashes are literal unless a quote follows */
    assert_string_equal(quote1("C:\\dir\\f.mp4"), "C:\\dir\\f.mp4");
    assert_string_equal(quote1("\\\\server\\share"), "\\\\server\\share");
    assert_string_equal(quote1("C:\\my dir\\f.mp4"), "\"C:\\my dir\\f.mp4\"");
    /* a quote: escaped, the backslashes before it doubled */
    assert_string_equal(quote1("a\"b"), "\"a\\\"b\"");
    assert_string_equal(quote1("a\\\"b"), "\"a\\\\\\\"b\"");
    assert_string_equal(quote1("\""), "\"\\\"\"");
    /* trailing backslashes before the closing quote: doubled */
    assert_string_equal(quote1("C:\\my dir\\"), "\"C:\\my dir\\\\\"");
    assert_string_equal(quote1("a b\\\\"), "\"a b\\\\\\\\\"");
    /* UTF-8 bytes pass through */
    assert_string_equal(quote1("caf\xc3\xa9.mp4"), "caf\xc3\xa9.mp4");
    assert_string_equal(quote1("caf\xc3\xa9 2.mp4"), "\"caf\xc3\xa9 2.mp4\"");
}

static void win_quote_arg_fits(void **state)
{
    (void)state;
    char buf[8];
    size_t n = 0;
    assert_true(xrd_win_quote_arg("abcdefg", buf, sizeof buf, &n)); /* 7 + NUL */
    n = 0;
    assert_false(xrd_win_quote_arg("abcdefgh", buf, sizeof buf, &n));
    n = 0;
    assert_true(xrd_win_quote_arg("a b c", buf, sizeof buf, &n)); /* "a b c" + NUL */
    assert_string_equal(buf, "\"a b c\"");
    n = 0;
    assert_false(xrd_win_quote_arg("a b cd", buf, sizeof buf, &n));
    n = 0;
    assert_false(xrd_win_quote_arg(NULL, buf, sizeof buf, &n));
}

/* The C runtime's split of a command line (the UCRT's parse_command_line,
 * as documented for main's arguments; CommandLineToArgvW agrees for these):
 * argv[0] up to whitespace, quotes only grouping; then 2n backslashes and a
 * quote give n backslashes and toggle quoting, 2n+1 give n and a literal
 * quote, backslashes elsewhere are literal, and in quotes "" is a quote. */
static int split_command_line(const char *s, char out[][256], int max)
{
    int argc = 0;
    /* argv[0] */
    {
        char *o = out[argc];
        bool q = false;
        while (*s && (q || (*s != ' ' && *s != '\t'))) {
            if (*s == '"')
                q = !q;
            else
                *o++ = *s;
            s++;
        }
        *o = '\0';
        argc++;
    }
    for (;;) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s || argc == max)
            break;
        char *o = out[argc];
        bool q = false;
        while (*s && (q || (*s != ' ' && *s != '\t'))) {
            size_t bs = 0;
            while (*s == '\\') {
                bs++;
                s++;
            }
            if (*s == '"') {
                for (size_t i = 0; i < bs / 2; i++)
                    *o++ = '\\';
                if (bs % 2) {
                    *o++ = '"';
                    s++;
                } else if (q && s[1] == '"') {
                    *o++ = '"';
                    s += 2;
                } else {
                    q = !q;
                    s++;
                }
            } else {
                for (size_t i = 0; i < bs; i++)
                    *o++ = '\\';
                if (*s && (q || (*s != ' ' && *s != '\t')))
                    *o++ = *s++;
            }
        }
        *o = '\0';
        argc++;
    }
    return argc;
}

static void win_command_line_round_trip(void **state)
{
    (void)state;
    const char *argv[] = { "C:\\Program Files\\ffmpeg\\ffprobe.exe", "-v", "error", "-i",
                           "D:\\films\\-weird \"name\" \\\\ & %x% ^.mp4", "", "\\\"",
                           "a\\\\", "tab\there\\", NULL };
    char line[1024], got[16][256];
    assert_int_equal(xrd_win_command_line(argv, line, sizeof line), 0);
    assert_string_equal(line, "\"C:\\Program Files\\ffmpeg\\ffprobe.exe\" -v error -i "
                              "\"D:\\films\\-weird \\\"name\\\" \\\\ & %x% ^.mp4\" \"\" "
                              "\"\\\\\\\"\" a\\\\ \"tab\there\\\\\"");
    int argc = split_command_line(line, got, 16);
    assert_int_equal(argc, 9);
    for (int i = 0; i < argc; i++)
        assert_string_equal(got[i], argv[i]);

    /* Every short string over the characters that matter comes back. */
    static const char alphabet[] = { 'a', ' ', '\t', '"', '\\', '\xc3' };
    unsigned seed = 12345;
    for (int round = 0; round < 3000; round++) {
        char args[3][16];
        const char *av[] = { "prog", args[0], args[1], args[2], NULL };
        for (int a = 0; a < 3; a++) {
            seed = seed * 1103515245u + 12345u;
            int len = (int)((seed >> 16) % 12);
            for (int i = 0; i < len; i++) {
                seed = seed * 1103515245u + 12345u;
                args[a][i] = alphabet[(seed >> 16) % sizeof alphabet];
            }
            args[a][len] = '\0';
        }
        assert_int_equal(xrd_win_command_line(av, line, sizeof line), 0);
        assert_int_equal(split_command_line(line, got, 16), 4);
        for (int i = 0; i < 4; i++)
            assert_string_equal(got[i], av[i]);
    }
}

static void win_command_line_refuses(void **state)
{
    (void)state;
    char line[64];
    const char *quote0[] = { "pro\"g", "x", NULL };
    assert_int_equal(xrd_win_command_line(quote0, line, sizeof line), -1);
    const char *empty0[] = { "", "x", NULL };
    assert_int_equal(xrd_win_command_line(empty0, line, sizeof line), -1);
    assert_int_equal(xrd_win_command_line(NULL, line, sizeof line), -1);
    const char *ok[] = { "a b", "c", NULL };
    char exact[8]; /* "a b" c + NUL */
    assert_int_equal(xrd_win_command_line(ok, exact, sizeof exact), 0);
    assert_string_equal(exact, "\"a b\" c");
    char short1[7];
    assert_int_equal(xrd_win_command_line(ok, short1, sizeof short1), -1);
    char short0[5]; /* not even argv[0] */
    assert_int_equal(xrd_win_command_line(ok, short0, sizeof short0), -1);
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
        cmocka_unit_test(image_formats),
        cmocka_unit_test(image_format_names),
        cmocka_unit_test(win_quote_arg_cases),
        cmocka_unit_test(win_quote_arg_fits),
        cmocka_unit_test(win_command_line_round_trip),
        cmocka_unit_test(win_command_line_refuses),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
