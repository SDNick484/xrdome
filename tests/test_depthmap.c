#define _XOPEN_SOURCE 700 /* mkdtemp, popen, setenv */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

#include "render/depthmap.h"

static char g_dir[64];

static int setup(void **state)
{
    (void)state;
    strcpy(g_dir, "/tmp/xrd_depth_XXXXXX");
    assert_non_null(mkdtemp(g_dir));
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
    return system(cmd) == 0 ? 0 : -1;
}

static void write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    assert_int_equal(fwrite(data, 1, len, f), len);
    assert_int_equal(fclose(f), 0);
}

/* ---- sha256 and the key -------------------------------------------------- */

static void sha256_vectors(void **state)
{
    (void)state;
    char hex[XRD_DEPTH_KEY_LEN + 1];
    xrd_sha256_hex("", 0, hex);
    assert_string_equal(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    xrd_sha256_hex("abc", 3, hex);
    assert_string_equal(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    /* 56 bytes: the length no longer fits the first block's padding */
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    xrd_sha256_hex(two, strlen(two), hex);
    assert_string_equal(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void key_is_sha256_of_the_file(void **state)
{
    (void)state;
    /* a million 'a's: many read blocks and update calls */
    size_t n = 1000000;
    char *data = malloc(n);
    assert_non_null(data);
    memset(data, 'a', n);
    char path[128];
    snprintf(path, sizeof path, "%s/a.jpg", g_dir);
    write_file(path, data, n);
    free(data);
    char key[XRD_DEPTH_KEY_LEN + 1];
    assert_int_equal(xrd_depth_key(path, key), 0);
    assert_string_equal(key, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    snprintf(path, sizeof path, "%s/missing.jpg", g_dir);
    assert_int_equal(xrd_depth_key(path, key), -1);
}

static void cache_dir_follows_xdg(void **state)
{
    (void)state;
    char buf[256];
    setenv("HOME", "/home/u", 1);
    setenv("XDG_CACHE_HOME", "/c", 1);
    assert_int_equal(xrd_depth_cache_dir(buf, sizeof buf), 0);
    assert_string_equal(buf, "/c/xrdome-tools/depth");
    setenv("XDG_CACHE_HOME", "relative", 1); /* invalid by the XDG spec: ignored */
    assert_int_equal(xrd_depth_cache_dir(buf, sizeof buf), 0);
    assert_string_equal(buf, "/home/u/.cache/xrdome-tools/depth");
    unsetenv("XDG_CACHE_HOME");
    assert_int_equal(xrd_depth_cache_dir(buf, sizeof buf), 0);
    assert_string_equal(buf, "/home/u/.cache/xrdome-tools/depth");
    unsetenv("HOME");
    assert_int_equal(xrd_depth_cache_dir(buf, sizeof buf), -1);
    assert_int_equal(xrd_depth_cache_dir(buf, 4), -1);
    assert_int_equal(xrd_depth_cache_path("/d", "ab", buf, sizeof buf), 0);
    assert_string_equal(buf, "/d/ab-dav2s-v1.pgm");
    assert_int_equal(xrd_depth_cache_path("/d", "ab", buf, 8), -1);
}

/* ---- PGM ------------------------------------------------------------------ */

static void pgm_good(void **state)
{
    (void)state;
    /* 3 x 2, as tools/tests/test_depth_cache.py writes it */
    const unsigned char f[] = "P5\n3 2\n65535\n\x00\x00\x00\x01\x01\x00\x01\x02\xff\xfe\xff\xff";
    xrd_depthmap m;
    char err[128] = "";
    assert_int_equal(xrd_depthmap_parse(f, sizeof f - 1, &m, err, sizeof err), XRD_DEPTH_OK);
    assert_int_equal(m.w, 3);
    assert_int_equal(m.h, 2);
    const uint16_t want[6] = {0, 1, 256, 258, 65534, 65535};
    for (int i = 0; i < 6; i++)
        assert_int_equal(m.px[i], want[i]);
    xrd_depthmap_free(&m);
    assert_null(m.px);
    /* other whitespace between the header fields is still a PGM */
    const unsigned char g[] = "P5 1\t1\r\n65535 \x12\x34";
    assert_int_equal(xrd_depthmap_parse(g, sizeof g - 1, &m, err, sizeof err), XRD_DEPTH_OK);
    assert_int_equal(m.px[0], 0x1234);
    xrd_depthmap_free(&m);
}

static void expect_bad(const char *bytes, size_t len, const char *needle)
{
    xrd_depthmap m;
    char err[128] = "";
    assert_int_equal(xrd_depthmap_parse((const unsigned char *)bytes, len, &m, err, sizeof err),
                     XRD_DEPTH_BAD);
    assert_null(m.px);
    assert_int_equal(m.w, 0);
    if (!strstr(err, needle))
        fail_msg("error '%s' does not contain '%s'", err, needle);
}

static void pgm_malformed(void **state)
{
    (void)state;
    expect_bad("", 0, "P5");
    expect_bad("P6\n1 1\n65535\n\0\0", 14, "P5");
    expect_bad("P5\n1 1\n255\n\0", 12, "maxval 255");
    expect_bad("P5\n1 1\n65535\n\0", 14, "1 bytes of samples");            /* truncated */
    expect_bad("P5\n1 1\n65535\n\0\0\0", 16, "3 bytes of samples");        /* trailing */
    expect_bad("P5\n0 1\n65535\n", 13, "outside");
    expect_bad("P5\n9000 1\n65535\n", 16, "outside");
    expect_bad("P5\n1\n", 5, "header");
    expect_bad("P5\nx 1\n65535\n\0\0", 15, "header");
    expect_bad("P51 1\n65535\n\0\0", 14, "header");                         /* no space after P5 */
    expect_bad("P5\n1 1\n65535", 12, "header");                             /* no byte after maxval */
    expect_bad("P5\n1 1 # c\n65535\n\0\0", 19, "header");                   /* comments are not written */
    expect_bad("P5\n12345678901 1\n65535\n", 23, "header");
}

static void pgm_read_file(void **state)
{
    (void)state;
    char path[128], err[128] = "";
    xrd_depthmap m;
    snprintf(path, sizeof path, "%s/none.pgm", g_dir);
    assert_int_equal(xrd_depthmap_read(path, &m, err, sizeof err), XRD_DEPTH_NONE);
    snprintf(path, sizeof path, "%s/x.pgm", g_dir);
    write_file(path, "P5\n2 1\n65535\n\x80\x00\xff\xff", 17);
    assert_int_equal(xrd_depthmap_read(path, &m, err, sizeof err), XRD_DEPTH_OK);
    assert_int_equal(m.w, 2);
    assert_int_equal(m.px[0], 0x8000);
    assert_int_equal(m.px[1], 0xffff);
    xrd_depthmap_free(&m);
    write_file(path, "P5\n2 1\n65535\n\x80", 14);
    assert_int_equal(xrd_depthmap_read(path, &m, err, sizeof err), XRD_DEPTH_BAD);
    /* a directory where the file should be */
    assert_int_equal(xrd_depthmap_read(g_dir, &m, err, sizeof err), XRD_DEPTH_BAD);
}

/* The contract end to end: tools/xrdome_depth/cache.py (the helper's writer)
 * writes a photo's cache file; the C side finds it by its own key and reads
 * the samples back. */
static void python_writer_cross_check(void **state)
{
    (void)state;
    char photo[128], cache[128], cmd[2048];
    snprintf(photo, sizeof photo, "%s/photo.jpg", g_dir);
    snprintf(cache, sizeof cache, "%s/cache", g_dir);
    unsigned char bytes[3000];
    for (size_t i = 0; i < sizeof bytes; i++)
        bytes[i] = (unsigned char)(i * 7 + 3);
    write_file(photo, bytes, sizeof bytes);
    snprintf(cmd, sizeof cmd,
             "'%s' -I -c 'import sys; sys.path.insert(0, sys.argv[1]); from xrdome_depth import "
             "cache; p = sys.argv[2]; d = sys.argv[3]; "
             "vals = [0, 1, 256, 258, 4660, 65534, 65535, 32768]; "
             "cache.write_pgm(cache.cache_path(d, cache.cache_key(p)), 4, 2, "
             "b\"\".join(v.to_bytes(2, \"big\") for v in vals))' '%s' '%s' '%s'",
             XRD_PYTHON, XRD_TOOLS_DIR, photo, cache);
    assert_int_equal(system(cmd), 0);
    char key[XRD_DEPTH_KEY_LEN + 1], path[256], err[128] = "";
    assert_int_equal(xrd_depth_key(photo, key), 0);
    assert_int_equal(xrd_depth_cache_path(cache, key, path, sizeof path), 0);
    xrd_depthmap m;
    assert_int_equal(xrd_depthmap_read(path, &m, err, sizeof err), XRD_DEPTH_OK);
    assert_int_equal(m.w, 4);
    assert_int_equal(m.h, 2);
    const uint16_t want[8] = {0, 1, 256, 258, 4660, 65534, 65535, 32768};
    for (int i = 0; i < 8; i++)
        assert_int_equal(m.px[i], want[i]);
    xrd_depthmap_free(&m);
}

/* ---- EXIF and the orientation policy ----------------------------------------- */

/* A minimal JPEG: SOI, an APP0, an APP1 Exif with one IFD0 entry (tag, type
 * SHORT, count 1, value) in the byte order be, then SOS. Returns its length. */
static size_t make_jpeg(unsigned char *out, bool be, unsigned tag, unsigned value)
{
    size_t n = 0;
    const unsigned char head[] = {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x04, 'J', 'F'};
    memcpy(out, head, sizeof head);
    n = sizeof head;
    unsigned char tiff[26];
    size_t t = 0;
#define P16(v) do { unsigned v_ = (v); if (be) { tiff[t++] = (unsigned char)(v_ >> 8); tiff[t++] = (unsigned char)v_; } \
                    else { tiff[t++] = (unsigned char)v_; tiff[t++] = (unsigned char)(v_ >> 8); } } while (0)
    tiff[t++] = be ? 'M' : 'I';
    tiff[t++] = be ? 'M' : 'I';
    P16(42);
    P16(0); /* IFD offset 8 as 32 bits: high half first in MM, low first in II */
    P16(0);
    if (be)
        tiff[7] = 8;
    else
        tiff[4] = 8;
    P16(1);
    P16(tag);
    P16(3);
    P16(be ? 0 : 1);
    P16(be ? 1 : 0);
    P16(value);
    P16(0);
#undef P16
    size_t seg = 2 + 6 + t;
    out[n++] = 0xff;
    out[n++] = 0xe1;
    out[n++] = (unsigned char)(seg >> 8);
    out[n++] = (unsigned char)seg;
    memcpy(out + n, "Exif\0\0", 6);
    n += 6;
    memcpy(out + n, tiff, t);
    n += t;
    const unsigned char sos[] = {0xff, 0xda, 0x00, 0x02};
    memcpy(out + n, sos, sizeof sos);
    return n + sizeof sos;
}

static void exif_orientation(void **state)
{
    (void)state;
    unsigned char j[128];
    for (unsigned o = 1; o <= 8; o++) {
        assert_int_equal(xrd_exif_orientation_jpeg(j, make_jpeg(j, true, 0x0112, o)), (int)o);
        assert_int_equal(xrd_exif_orientation_jpeg(j, make_jpeg(j, false, 0x0112, o)), (int)o);
    }
    assert_int_equal(xrd_exif_orientation_jpeg(j, make_jpeg(j, true, 0x0112, 9)), 0);
    assert_int_equal(xrd_exif_orientation_jpeg(j, make_jpeg(j, true, 0x0110, 6)), 0); /* other tag */
    size_t n = make_jpeg(j, true, 0x0112, 6);
    assert_int_equal(xrd_exif_orientation_jpeg(j, n - 12), 0); /* cut inside the segment */
    j[0] = 0x89;                                               /* not a JPEG */
    assert_int_equal(xrd_exif_orientation_jpeg(j, n), 0);
    const unsigned char bare[] = {0xff, 0xd8, 0xff, 0xda, 0x00, 0x02};
    assert_int_equal(xrd_exif_orientation_jpeg(bare, sizeof bare), 0);

    char path[128];
    snprintf(path, sizeof path, "%s/o5.jpg", g_dir);
    write_file(path, j, 0);
    assert_int_equal(xrd_exif_orientation(path), 0);
    n = make_jpeg(j, false, 0x0112, 5);
    write_file(path, j, n);
    assert_int_equal(xrd_exif_orientation(path), 5);
    snprintf(path, sizeof path, "%s/missing.jpg", g_dir);
    assert_int_equal(xrd_exif_orientation(path), 0);
}

static void orientation_policy(void **state)
{
    (void)state;
    for (int o = 0; o <= 8; o++) {
        bool mirrored = o == 2 || o == 4 || o == 5 || o == 7;
        assert_int_equal(xrd_exif_mirrored(o), mirrored);
        assert_true(xrd_depth_orientation_ok(o, 0, 42));
        assert_true(xrd_depth_orientation_ok(o, 1, 0));
        assert_int_equal(xrd_depth_orientation_ok(o, 0, 37), !mirrored);
        assert_int_equal(xrd_depth_orientation_ok(o, 0, 41), !mirrored);
        assert_int_equal(xrd_depth_orientation_ok(o, 0, 0), !mirrored); /* version unknown */
    }
}

static void mpv_version(void **state)
{
    (void)state;
    int a, b;
    assert_true(xrd_mpv_version_parse("mpv v0.42.0", &a, &b));
    assert_int_equal(a, 0);
    assert_int_equal(b, 42);
    assert_true(xrd_mpv_version_parse("mpv 0.37.0", &a, &b));
    assert_int_equal(b, 37);
    assert_true(xrd_mpv_version_parse("mpv v0.40.0-123-gabcdef", &a, &b));
    assert_int_equal(b, 40);
    assert_true(xrd_mpv_version_parse("mpv 1.2", &a, &b));
    assert_int_equal(a, 1);
    assert_int_equal(b, 2);
    assert_false(xrd_mpv_version_parse("mpv git-abc", &a, &b));
    assert_int_equal(a, 0);
    assert_int_equal(b, 0);
    assert_false(xrd_mpv_version_parse("", &a, &b));
    assert_false(xrd_mpv_version_parse(NULL, &a, &b));
}

static void aspect(void **state)
{
    (void)state;
    assert_true(xrd_depth_aspect_matches(2048, 1536, 4032, 3024));
    assert_true(xrd_depth_aspect_matches(2048, 1365, 4000, 2667)); /* rounded */
    assert_true(xrd_depth_aspect_matches(96, 48, 96, 48));
    assert_false(xrd_depth_aspect_matches(1536, 2048, 4032, 3024)); /* turned */
    assert_false(xrd_depth_aspect_matches(48, 96, 96, 48));
    assert_false(xrd_depth_aspect_matches(2048, 1500, 4032, 3024));
    assert_false(xrd_depth_aspect_matches(0, 1, 1, 1));
    assert_false(xrd_depth_aspect_matches(1, 1, 0, 1));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(sha256_vectors),
        cmocka_unit_test_setup_teardown(key_is_sha256_of_the_file, setup, teardown),
        cmocka_unit_test(cache_dir_follows_xdg),
        cmocka_unit_test(pgm_good),
        cmocka_unit_test(pgm_malformed),
        cmocka_unit_test_setup_teardown(pgm_read_file, setup, teardown),
        cmocka_unit_test_setup_teardown(python_writer_cross_check, setup, teardown),
        cmocka_unit_test_setup_teardown(exif_orientation, setup, teardown),
        cmocka_unit_test(orientation_policy),
        cmocka_unit_test(mpv_version),
        cmocka_unit_test(aspect),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
