#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "values.h"

#include "detect/detect.h"

static xrd_probe mk(int w, int h)
{
    xrd_probe p;
    memset(&p, 0, sizeof p);
    p.have_size = w > 0 && h > 0;
    p.width = w; p.height = h; p.sar_num = 1; p.sar_den = 1;
    return p;
}

static xrd_detect resolve(const xrd_probe *p, const char *path)
{
    xrd_detect d;
    xrd_detect_resolve(NULL, NULL, p, path, &d);
    return d;
}

static void expect(const xrd_detect *d, xrd_proj proj, xrd_layout layout)
{
    assert_int_equal(d->proj, proj);
    assert_int_equal(d->layout, layout);
}


/* ---- shared cases: the TSV files in tests/data, also read by test_formats.py ---- */

/* Next data row of `f` split at tabs into cols[0..n-1]; false at end of file.
 * Blank lines and lines starting with '#' are skipped. `line` is the buffer. */
static bool tsv_row(FILE *f, char *line, size_t len, char **cols, int n)
{
    while (fgets(line, (int)len, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = '\0';
        if (!l || line[0] == '#')
            continue;
        char *p = line;
        for (int i = 0; i < n; i++) {
            cols[i] = p;
            char *tab = strchr(p, '\t');
            if (!tab) {
                assert_int_equal(i, n - 1);
                break;
            }
            *tab = '\0';
            p = tab + 1;
        }
        return true;
    }
    return false;
}

static FILE *open_data(const char *file)
{
    const char *dir = getenv("XRD_TEST_DATA");
    assert_non_null(dir);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    FILE *f = fopen(path, "r");
    assert_non_null(f);
    return f;
}

/* ---- name hints ---- */

static void hints(const char *path, xrd_detect_given *g)
{
    char a[16], b[16], c[16];
    xrd_detect_name_hints(path, g, a, b, c);
}

static void name_table(void **s)
{
    (void)s;
    FILE *f = open_data("detect_names.tsv");
    char line[512], *c[4];
    int rows = 0;
    while (tsv_row(f, line, sizeof line, c, 4)) {
        bool proj = strcmp(c[1], "-") != 0, lay = strcmp(c[2], "-") != 0, pk = strcmp(c[3], "-") != 0;
        xrd_proj p = 0; xrd_layout l = 0; xrd_packing k = 0;
        assert_true(!proj || xrd_value_proj(c[1], &p));
        assert_true(!lay || xrd_value_layout(c[2], &l));
        assert_true(!pk || xrd_value_packing(c[3], &k));
        xrd_detect_given g;
        hints(c[0], &g);
        assert_int_equal(g.have_proj, proj);
        if (proj) assert_int_equal(g.proj, p);
        assert_int_equal(g.have_layout, lay);
        if (lay) assert_int_equal(g.layout, l);
        assert_int_equal(g.have_packing, pk);
        if (pk) assert_int_equal(g.packing, k);
        rows++;
    }
    fclose(f);
    assert_int_equal(rows, 22);
}

static void name_examples(void **s)
{
    (void)s;
    xrd_detect_given g; char a[16], b[16], c[16];
    xrd_detect_name_hints("/films/movie.HSBS.1080p.mkv", &g, a, b, c);
    assert_true(g.have_layout); assert_int_equal(g.layout, XRD_LAYOUT_SBS_LR);
    assert_true(g.have_packing); assert_int_equal(g.packing, XRD_PACKING_HALF);
    assert_false(g.have_proj);
    assert_string_equal(b, "HSBS"); assert_string_equal(c, "HSBS");

    hints("Routes.1800.mkv", &g);
    assert_false(g.have_proj || g.have_layout || g.have_packing);
    hints("ou.est.tb.mkv", &g);
    assert_false(g.have_proj || g.have_layout || g.have_packing);
    hints("lr.rl.mkv", &g);
    assert_false(g.have_layout);
    hints("A_OU.mkv", &g);
    assert_true(g.have_layout); assert_int_equal(g.layout, XRD_LAYOUT_TB_LR);
    hints("clip_3D.mp4", &g);
    assert_true(g.have_layout); assert_int_equal(g.layout, XRD_LAYOUT_SBS_LR);
    hints("x_180_LR.mp4", &g);
    assert_true(g.have_proj); assert_int_equal(g.proj, XRD_PROJ_HEQUIRECT);
    assert_true(g.have_layout); assert_int_equal(g.layout, XRD_LAYOUT_SBS_LR);
    /* 3D loses to an explicit layout token, whichever comes first. */
    hints("3D.film.OU.mkv", &g);
    assert_int_equal(g.layout, XRD_LAYOUT_TB_LR);
    /* The directory is not part of the name. */
    hints("/home/SBS/movie.mkv", &g);
    assert_false(g.have_layout);
    hints(NULL, &g);
    assert_false(g.have_layout);
}

/* ---- shape ---- */

static void shape_nothing_known(void **s)
{
    (void)s;
    xrd_probe p = mk(2880, 1440);
    xrd_detect d = resolve(&p, "x.mkv");
    expect(&d, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    assert_int_equal(d.proj_src, XRD_SRC_SHAPE); assert_int_equal(d.layout_src, XRD_SRC_SHAPE);
    assert_int_equal(d.note_count, 0);
    p = mk(4320, 2160);
    d = resolve(&p, "x.mp4");
    expect(&d, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    assert_int_equal(d.proj_src, XRD_SRC_SHAPE); assert_int_equal(d.layout_src, XRD_SRC_SHAPE);

    FILE *f = open_data("detect_shapes.tsv");
    char line[256], *c[4];
    int rows = 0;
    while (tsv_row(f, line, sizeof line, c, 4)) {
        xrd_proj want_proj; xrd_layout want_lay;
        assert_true(xrd_value_proj(c[2], &want_proj));
        assert_true(xrd_value_layout(c[3], &want_lay));
        p = mk(atoi(c[0]), atoi(c[1]));
        d = resolve(&p, "x.mkv");
        expect(&d, want_proj, want_lay);
        assert_int_equal(d.proj_src, XRD_SRC_SHAPE);
        assert_int_equal(d.layout_src, XRD_SRC_SHAPE);
        assert_int_equal(d.packing, XRD_PACKING_AUTO);
        assert_int_equal(d.packing_src, XRD_SRC_AUTO);
        rows++;
    }
    fclose(f);
    assert_int_equal(rows, 9);
    /* 0.889 +- 3% */
    p = mk(1920, 2160 * 1.025);
    expect((d = resolve(&p, "x"), &d), XRD_PROJ_FLAT, XRD_LAYOUT_TB_LR);
    p = mk(1920, 2160 * 1.05);
    expect((d = resolve(&p, "x"), &d), XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
}

static xrd_detect resolve_flags(const xrd_detect_given *flags, const xrd_probe *p, const char *path)
{
    xrd_detect d;
    xrd_detect_resolve(flags, NULL, p, path, &d);
    return d;
}

static void shape_one_known(void **s)
{
    (void)s;
    xrd_probe p;
    xrd_detect_given g;
    xrd_detect d;
    /* projection only: layout known */
    struct { xrd_layout lay; int w, h; xrd_proj want; } prow[] = {
        { XRD_LAYOUT_SBS_LR, 2880, 1440, XRD_PROJ_HEQUIRECT },
        { XRD_LAYOUT_SBS_RL, 2880, 1440, XRD_PROJ_HEQUIRECT },
        { XRD_LAYOUT_SBS_LR, 1920, 1080, XRD_PROJ_FLAT },
        { XRD_LAYOUT_SBS_LR, 3840, 1080, XRD_PROJ_FLAT },
        { XRD_LAYOUT_TB_LR, 1000, 2000, XRD_PROJ_HEQUIRECT },
        { XRD_LAYOUT_TB_RL, 1000, 2000, XRD_PROJ_HEQUIRECT },
        { XRD_LAYOUT_TB_LR, 1920, 1080, XRD_PROJ_FLAT },
        { XRD_LAYOUT_MONO, 1000, 1000, XRD_PROJ_HEQUIRECT },
        { XRD_LAYOUT_MONO, 1920, 1080, XRD_PROJ_FLAT },
    };
    for (size_t i = 0; i < sizeof prow / sizeof prow[0]; i++) {
        memset(&g, 0, sizeof g);
        g.have_layout = true; g.layout = prow[i].lay;
        p = mk(prow[i].w, prow[i].h);
        d = resolve_flags(&g, &p, "x.mkv");
        assert_int_equal(d.proj, prow[i].want);
        assert_int_equal(d.proj_src, XRD_SRC_SHAPE);
        assert_int_equal(d.layout, prow[i].lay);
        assert_int_equal(d.layout_src, XRD_SRC_FLAG);
    }
    /* layout only: projection known */
    struct { xrd_proj proj; int w, h; xrd_layout want; } lrow[] = {
        { XRD_PROJ_HEQUIRECT, 1000, 1000, XRD_LAYOUT_MONO },
        { XRD_PROJ_HEQUIRECT, 1000, 2000, XRD_LAYOUT_TB_LR },
        { XRD_PROJ_HEQUIRECT, 2880, 1440, XRD_LAYOUT_SBS_LR },
        { XRD_PROJ_HEQUIRECT, 1920, 1080, XRD_LAYOUT_SBS_LR },
        { XRD_PROJ_FLAT, 1920, 1080, XRD_LAYOUT_MONO },
        { XRD_PROJ_FLAT, 3840, 1080, XRD_LAYOUT_SBS_LR },
        { XRD_PROJ_FLAT, 1920, 2160, XRD_LAYOUT_TB_LR },
    };
    for (size_t i = 0; i < sizeof lrow / sizeof lrow[0]; i++) {
        memset(&g, 0, sizeof g);
        g.have_proj = true; g.proj = lrow[i].proj;
        p = mk(lrow[i].w, lrow[i].h);
        d = resolve_flags(&g, &p, "x.mkv");
        assert_int_equal(d.layout, lrow[i].want);
        assert_int_equal(d.layout_src, XRD_SRC_SHAPE);
        assert_int_equal(d.proj, lrow[i].proj);
        assert_int_equal(d.proj_src, XRD_SRC_FLAG);
    }
}

/* ---- metadata ---- */

static void metadata_rows(void **s)
{
    (void)s;
    xrd_probe p = mk(1920, 1080);
    xrd_detect d;
    p.stereo = XRD_STEREO_SBS;
    d = resolve(&p, "x.mkv");
    assert_int_equal(d.layout, XRD_LAYOUT_SBS_LR); assert_int_equal(d.layout_src, XRD_SRC_METADATA);
    assert_int_equal(d.proj, XRD_PROJ_FLAT); assert_int_equal(d.proj_src, XRD_SRC_SHAPE);
    p.stereo_inverted = true;
    d = resolve(&p, "x.mkv");
    assert_int_equal(d.layout, XRD_LAYOUT_SBS_RL);
    p.stereo = XRD_STEREO_TB;
    d = resolve(&p, "x.mkv");
    assert_int_equal(d.layout, XRD_LAYOUT_TB_RL);
    p.stereo_inverted = false;
    d = resolve(&p, "x.mkv");
    assert_int_equal(d.layout, XRD_LAYOUT_TB_LR);
    p.stereo = XRD_STEREO_2D;
    d = resolve(&p, "x.mkv");
    assert_int_equal(d.layout, XRD_LAYOUT_MONO); assert_int_equal(d.layout_src, XRD_SRC_METADATA);
    assert_int_equal(d.note_count, 0);

    /* other stereo type: ignored, one note naming it, shape decides */
    p.stereo = XRD_STEREO_OTHER;
    strcpy(p.stereo_name, "checkerboard");
    d = resolve(&p, "x.mkv");
    assert_int_equal(d.note_count, 1);
    assert_non_null(strstr(d.notes[0], "checkerboard"));
    assert_int_equal(d.layout_src, XRD_SRC_SHAPE);

    /* spherical */
    p = mk(1920, 1080);
    p.spherical = XRD_SPH_EQUIRECT;
    strcpy(p.spherical_name, "equirectangular");
    d = resolve(&p, "x.mkv");
    assert_int_equal(d.proj, XRD_PROJ_HEQUIRECT); assert_int_equal(d.proj_src, XRD_SRC_METADATA);
    assert_int_equal(d.note_count, 0);
    assert_int_equal(d.layout_src, XRD_SRC_SHAPE);
    assert_int_equal(d.layout, XRD_LAYOUT_SBS_LR); /* hequirect default for 16:9 */

    p.spherical = XRD_SPH_OTHER;
    strcpy(p.spherical_name, "cubemap");
    d = resolve(&p, "x.mkv");
    assert_int_equal(d.proj, XRD_PROJ_HEQUIRECT); assert_int_equal(d.proj_src, XRD_SRC_METADATA);
    assert_int_equal(d.note_count, 1);
    assert_non_null(strstr(d.notes[0], "cubemap"));
}

static void metadata_with_fixtures_shape(void **s)
{
    (void)s;
    /* Spherical + stereo, owner style: hequirect and sbs from metadata, no notes. */
    xrd_probe p = mk(2880, 1440);
    p.spherical = XRD_SPH_EQUIRECT; p.stereo = XRD_STEREO_SBS;
    xrd_detect d = resolve(&p, "x.mkv");
    expect(&d, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    assert_int_equal(d.proj_src, XRD_SRC_METADATA);
    assert_int_equal(d.layout_src, XRD_SRC_METADATA);
}

/* ---- precedence ---- */

static void precedence(void **s)
{
    (void)s;
    xrd_probe p = mk(2880, 1440);          /* shape says hequirect sbs-lr */
    p.stereo = XRD_STEREO_TB;              /* metadata says tb-lr */
    p.spherical = XRD_SPH_EQUIRECT;        /* metadata says hequirect */
    const char *path = "x.FSBS.flat.mkv";  /* name: sbs-lr, full */
    xrd_detect_given flags, rec;
    xrd_detect d;

    memset(&flags, 0, sizeof flags); memset(&rec, 0, sizeof rec);
    xrd_detect_resolve(NULL, NULL, &p, path, &d);
    assert_int_equal(d.layout, XRD_LAYOUT_TB_LR); assert_int_equal(d.layout_src, XRD_SRC_METADATA);
    assert_int_equal(d.proj, XRD_PROJ_HEQUIRECT); assert_int_equal(d.proj_src, XRD_SRC_METADATA);
    assert_int_equal(d.packing, XRD_PACKING_FULL); assert_int_equal(d.packing_src, XRD_SRC_NAME);
    assert_string_equal(d.packing_token, "FSBS");

    /* name beats shape */
    p.stereo = XRD_STEREO_NONE; p.spherical = XRD_SPH_NONE;
    xrd_detect_resolve(NULL, NULL, &p, path, &d);
    assert_int_equal(d.layout, XRD_LAYOUT_SBS_LR); assert_int_equal(d.layout_src, XRD_SRC_NAME);
    assert_string_equal(d.layout_token, "FSBS");
    assert_int_equal(d.proj_src, XRD_SRC_SHAPE);

    /* record beats metadata and name */
    p.stereo = XRD_STEREO_TB;
    rec.have_layout = true; rec.layout = XRD_LAYOUT_MONO;
    rec.have_packing = true; rec.packing = XRD_PACKING_HALF;
    xrd_detect_resolve(NULL, &rec, &p, path, &d);
    assert_int_equal(d.layout, XRD_LAYOUT_MONO); assert_int_equal(d.layout_src, XRD_SRC_RECORD);
    assert_int_equal(d.packing, XRD_PACKING_HALF); assert_int_equal(d.packing_src, XRD_SRC_RECORD);
    /* a record that sets only layout leaves proj to lower sources */
    assert_int_equal(d.proj_src, XRD_SRC_SHAPE);

    /* flag beats record */
    flags.have_layout = true; flags.layout = XRD_LAYOUT_TB_RL;
    flags.have_proj = true; flags.proj = XRD_PROJ_FLAT;
    xrd_detect_resolve(&flags, &rec, &p, path, &d);
    assert_int_equal(d.layout, XRD_LAYOUT_TB_RL); assert_int_equal(d.layout_src, XRD_SRC_FLAG);
    assert_int_equal(d.proj, XRD_PROJ_FLAT); assert_int_equal(d.proj_src, XRD_SRC_FLAG);
    assert_int_equal(d.packing_src, XRD_SRC_RECORD);

    /* a record that sets only proj leaves layout to lower sources */
    memset(&rec, 0, sizeof rec);
    rec.have_proj = true; rec.proj = XRD_PROJ_FLAT;
    xrd_detect_resolve(NULL, &rec, &p, "plain.mkv", &d);
    assert_int_equal(d.proj_src, XRD_SRC_RECORD);
    assert_int_equal(d.layout, XRD_LAYOUT_TB_LR); assert_int_equal(d.layout_src, XRD_SRC_METADATA);
}

/* ---- fallback and aspect ---- */

static void no_probe_fallback(void **s)
{
    (void)s;
    xrd_detect d = resolve(NULL, "x.mkv");
    expect(&d, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    assert_int_equal(d.proj_src, XRD_SRC_FALLBACK);
    assert_int_equal(d.layout_src, XRD_SRC_FALLBACK);
    assert_int_equal(d.note_count, 1);
    assert_true(d.frame_aspect == 0.0);

    /* a probe without a size is the same */
    xrd_probe p = mk(0, 0);
    d = resolve(&p, "x.mkv");
    expect(&d, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    assert_int_equal(d.note_count, 1);

    /* nothing open, no note */
    xrd_detect_given f = { true, true, false, XRD_PROJ_FLAT, XRD_LAYOUT_MONO, 0 };
    d = resolve_flags(&f, NULL, "x.mkv");
    assert_int_equal(d.note_count, 0);

    /* a flat file with no size is not split in two */
    xrd_detect_given fp = { true, false, false, XRD_PROJ_FLAT, 0, 0 };
    d = resolve_flags(&fp, NULL, "x.mkv");
    expect(&d, XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
}

static void frame_aspect_values(void **s)
{
    (void)s;
    xrd_probe p = mk(720, 576);
    p.sar_num = 16; p.sar_den = 15;
    xrd_detect d = resolve(&p, "x.mkv");
    assert_float_equal(d.frame_aspect, 720.0 * 16 / (576.0 * 15), 1e-9);

    /* stereo metadata forces a 1:1 pixel aspect */
    p = mk(1920, 1080);
    p.sar_num = 1; p.sar_den = 2;
    p.stereo = XRD_STEREO_SBS;
    d = resolve(&p, "x.mkv");
    assert_float_equal(d.frame_aspect, 1920.0 / 1080.0, 1e-9);
    p.stereo = XRD_STEREO_NONE;
    d = resolve(&p, "x.mkv");
    assert_float_equal(d.frame_aspect, 1920.0 / 2160.0, 1e-9);
}

/* ---- relayout ---- */

static void relayout(void **s)
{
    (void)s;
    xrd_layout l; xrd_packing k = XRD_PACKING_FULL;
    xrd_detect_relayout(2.0, XRD_PROJ_FLAT, &l, &k);
    assert_int_equal(l, XRD_LAYOUT_MONO); assert_int_equal(k, XRD_PACKING_AUTO);
    xrd_detect_relayout(16.0 / 9.0, XRD_PROJ_HEQUIRECT, &l, &k);
    assert_int_equal(l, XRD_LAYOUT_SBS_LR);
    xrd_detect_relayout(0.889, XRD_PROJ_FLAT, &l, &k);
    assert_int_equal(l, XRD_LAYOUT_TB_LR);
    xrd_detect_relayout(0, XRD_PROJ_FLAT, &l, &k);
    assert_int_equal(l, XRD_LAYOUT_MONO);
    xrd_detect_relayout(0, XRD_PROJ_HEQUIRECT, &l, &k);
    assert_int_equal(l, XRD_LAYOUT_SBS_LR);
    xrd_detect_relayout(1.0, XRD_PROJ_HEQUIRECT, &l, &k);
    assert_int_equal(l, XRD_LAYOUT_MONO);
    xrd_detect_relayout(0.5, XRD_PROJ_HEQUIRECT, &l, &k);
    assert_int_equal(l, XRD_LAYOUT_TB_LR);
    xrd_detect_relayout(3.5, XRD_PROJ_FLAT, &l, &k);
    assert_int_equal(l, XRD_LAYOUT_SBS_LR);
}

/* ---- describe ---- */

static void describe_examples(void **s)
{
    (void)s;
    char buf[300];
    xrd_probe p = mk(2880, 1440);
    xrd_detect d = resolve(&p, "x.mkv");
    assert_true(xrd_detect_describe(&d, buf, sizeof buf) > 0);
    assert_string_equal(buf, "proj=hequirect (frame shape) layout=sbs-lr (frame shape)");

    p = mk(1920, 1080);
    d = resolve(&p, "/v/Movie.HSBS.mkv");
    int n = xrd_detect_describe(&d, buf, sizeof buf);
    assert_string_equal(buf, "proj=flat (frame shape) layout=sbs-lr (file name \"HSBS\") "
                             "packing=half (file name \"HSBS\")");
    assert_int_equal(n, (int)strlen(buf));

    p = mk(1920, 2160);
    p.stereo = XRD_STEREO_TB;
    xrd_detect_given f = { true, false, false, XRD_PROJ_FLAT, 0, 0 };
    d = resolve_flags(&f, &p, "x.mkv");
    xrd_detect_describe(&d, buf, sizeof buf);
    assert_string_equal(buf, "proj=flat (flag) layout=tb-lr (metadata) packing=full (auto)");

    /* mono flat shows no packing; fallback is named */
    p = mk(1920, 1080);
    d = resolve(&p, "x.mkv");
    xrd_detect_describe(&d, buf, sizeof buf);
    assert_string_equal(buf, "proj=flat (frame shape) layout=mono (frame shape)");
    d = resolve(NULL, "x.mkv");
    xrd_detect_describe(&d, buf, sizeof buf);
    assert_string_equal(buf, "proj=hequirect (fallback) layout=sbs-lr (fallback)");

    /* auto packing for a half SBS shape, and a record source */
    p = mk(1920, 1080);
    xrd_detect_given r = { false, true, false, 0, XRD_LAYOUT_SBS_LR, 0 };
    xrd_detect_resolve(NULL, &r, &p, "x.mkv", &d);
    xrd_detect_describe(&d, buf, sizeof buf);
    assert_string_equal(buf, "proj=flat (frame shape) layout=sbs-lr (record) packing=half (auto)");

    /* too small a buffer */
    assert_int_equal(xrd_detect_describe(&d, buf, 10), -1);
    assert_int_equal(xrd_detect_describe(&d, buf, 0), -1);
}

/* ---- photos ---- */

static xrd_probe image_probe(int w, int h)
{
    xrd_probe p = mk(w, h);
    p.image = true;
    return p;
}

static void image_is_flat_mono(void **s)
{
    (void)s;
    /* Whatever the shape says: 2:1 would be VR180 SBS, 3:1 flat SBS, for a film. */
    int shapes[][2] = {{4000, 3000}, {3000, 4000}, {2000, 1000}, {3000, 1000}, {1000, 1125}};
    for (size_t i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        xrd_probe p = image_probe(shapes[i][0], shapes[i][1]);
        xrd_detect d = resolve(&p, "/photos/IMG_0001.jpg");
        expect(&d, XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
        assert_true(d.image);
        assert_int_equal(d.proj_src, XRD_SRC_IMAGE);
        assert_int_equal(d.layout_src, XRD_SRC_IMAGE);
        assert_int_equal(d.note_count, 0);
        /* The probe's size is the stored one; EXIF may turn it. */
        assert_true(d.frame_aspect == 0.0);
    }
    xrd_probe p = image_probe(4000, 3000);
    char buf[200];
    xrd_detect d = resolve(&p, "a.jpg");
    assert_true(xrd_detect_describe(&d, buf, sizeof buf) > 0);
    assert_string_equal(buf, "proj=flat (image file) layout=mono (image file)");
}

static void image_other_sources_win(void **s)
{
    (void)s;
    xrd_probe p = image_probe(4000, 2000);
    /* The name says what it says: a side-by-side photo pair. */
    xrd_detect d = resolve(&p, "holiday_SBS.jpg");
    expect(&d, XRD_PROJ_FLAT, XRD_LAYOUT_SBS_LR);
    assert_int_equal(d.proj_src, XRD_SRC_IMAGE);
    assert_int_equal(d.layout_src, XRD_SRC_NAME);
    assert_true(d.image);
    /* A flag and a record beat the image default. */
    xrd_detect_given flags, record;
    memset(&flags, 0, sizeof flags);
    memset(&record, 0, sizeof record);
    flags.have_proj = true; flags.proj = XRD_PROJ_HEQUIRECT;
    record.have_layout = true; record.layout = XRD_LAYOUT_TB_LR;
    xrd_detect_resolve(&flags, &record, &p, "pano.jpg", &d);
    expect(&d, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_TB_LR);
    assert_int_equal(d.proj_src, XRD_SRC_FLAG);
    assert_int_equal(d.layout_src, XRD_SRC_RECORD);
    /* A record saved with the photo: flat mono is what it says. */
    memset(&flags, 0, sizeof flags);
    memset(&record, 0, sizeof record);
    record.have_proj = true; record.proj = XRD_PROJ_FLAT;
    record.have_layout = true; record.layout = XRD_LAYOUT_MONO;
    xrd_detect_resolve(&flags, &record, &p, "a.jpg", &d);
    expect(&d, XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
    assert_int_equal(d.proj_src, XRD_SRC_RECORD);
}

static void image_by_name_without_probe(void **s)
{
    (void)s;
    xrd_detect d = resolve(NULL, "/photos/IMG_0001.JPG");
    assert_true(d.image);
    expect(&d, XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
    assert_int_equal(d.note_count, 0); /* no "could not detect" note for a photo */
    /* A probe that says it is not an image outranks the extension. */
    xrd_probe p = mk(1920, 1080);
    d = resolve(&p, "/films/clip.jpg");
    assert_false(d.image);
    /* And a film is not an image. */
    d = resolve(NULL, "/films/clip.mp4");
    assert_false(d.image);
    expect(&d, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
}

static void image_names(void **s)
{
    (void)s;
    const char *yes[] = {"a.jpg", "a.JPG", "a.jpeg", "/x/y/a.PnG", "a.webp", "a.bmp", "a.tif",
                         "a.TIFF", "my.photo.jpg", "dir.d/a.jpg"};
    const char *no[] = {"", "a", "a.mp4", "jpg", ".jpg.txt", "a.jpg.mp4", "a.gif", "a.heic",
                        "dir.jpg/a", "a.jpgx", "a."};
    for (size_t i = 0; i < sizeof yes / sizeof yes[0]; i++)
        assert_true(xrd_detect_image_name(yes[i]));
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++)
        assert_false(xrd_detect_image_name(no[i]));
    assert_false(xrd_detect_image_name(NULL));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(name_table),
        cmocka_unit_test(name_examples),
        cmocka_unit_test(shape_nothing_known),
        cmocka_unit_test(shape_one_known),
        cmocka_unit_test(metadata_rows),
        cmocka_unit_test(metadata_with_fixtures_shape),
        cmocka_unit_test(precedence),
        cmocka_unit_test(no_probe_fallback),
        cmocka_unit_test(frame_aspect_values),
        cmocka_unit_test(relayout),
        cmocka_unit_test(describe_examples),
        cmocka_unit_test(image_is_flat_mono),
        cmocka_unit_test(image_other_sources_win),
        cmocka_unit_test(image_by_name_without_probe),
        cmocka_unit_test(image_names),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
