#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>
#include <cmocka.h>

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

/* ---- name hints ---- */

static void hints(const char *path, xrd_detect_given *g)
{
    char a[16], b[16], c[16];
    xrd_detect_name_hints(path, g, a, b, c);
}

static void name_table(void **s)
{
    (void)s;
    struct { const char *name; bool proj; xrd_proj p; bool lay; xrd_layout l; bool pk; xrd_packing k; } rows[] = {
        { "a.VR180.mp4", true, XRD_PROJ_HEQUIRECT, false, 0, false, 0 },
        { "a.vr180.mp4", true, XRD_PROJ_HEQUIRECT, false, 0, false, 0 },
        { "a_180.mp4", true, XRD_PROJ_HEQUIRECT, false, 0, false, 0 },
        { "a 180x180 b.mp4", true, XRD_PROJ_HEQUIRECT, false, 0, false, 0 },
        { "a-VR-b.mp4", true, XRD_PROJ_HEQUIRECT, false, 0, false, 0 },
        { "a.SBS.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, false, 0 },
        { "a.sbs.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, false, 0 },
        { "a.3DH.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, false, 0 },
        { "a.LR.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, false, 0 },
        { "a.RL.mp4", false, 0, true, XRD_LAYOUT_SBS_RL, false, 0 },
        { "a.HSBS.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, true, XRD_PACKING_HALF },
        { "a.hsbs.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, true, XRD_PACKING_HALF },
        { "a.FSBS.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, true, XRD_PACKING_FULL },
        { "a.FULLSBS.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, true, XRD_PACKING_FULL },
        { "a.TB.mp4", false, 0, true, XRD_LAYOUT_TB_LR, false, 0 },
        { "a.OU.mp4", false, 0, true, XRD_LAYOUT_TB_LR, false, 0 },
        { "a.3DV.mp4", false, 0, true, XRD_LAYOUT_TB_LR, false, 0 },
        { "a.HTB.mp4", false, 0, true, XRD_LAYOUT_TB_LR, true, XRD_PACKING_HALF },
        { "a.HOU.mp4", false, 0, true, XRD_LAYOUT_TB_LR, true, XRD_PACKING_HALF },
        { "a.MONO.mp4", false, 0, true, XRD_LAYOUT_MONO, false, 0 },
        { "a.2d.mp4", false, 0, true, XRD_LAYOUT_MONO, false, 0 },
        { "a.3D.mp4", false, 0, true, XRD_LAYOUT_SBS_LR, false, 0 },
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        xrd_detect_given g;
        hints(rows[i].name, &g);
        assert_int_equal(g.have_proj, rows[i].proj);
        if (rows[i].proj) assert_int_equal(g.proj, rows[i].p);
        assert_int_equal(g.have_layout, rows[i].lay);
        if (rows[i].lay) assert_int_equal(g.layout, rows[i].l);
        assert_int_equal(g.have_packing, rows[i].pk);
        if (rows[i].pk) assert_int_equal(g.packing, rows[i].k);
    }
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

    struct { int w, h; xrd_proj proj; xrd_layout lay; } rows[] = {
        { 1920, 1080, XRD_PROJ_FLAT, XRD_LAYOUT_MONO },
        { 3840, 1080, XRD_PROJ_FLAT, XRD_LAYOUT_SBS_LR },
        { 1920, 2160, XRD_PROJ_FLAT, XRD_LAYOUT_TB_LR },
        { 1080, 1080, XRD_PROJ_FLAT, XRD_LAYOUT_MONO },
        { 3840, 2160, XRD_PROJ_FLAT, XRD_LAYOUT_MONO },
        { 1960, 1000, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR },
        { 1940, 1000, XRD_PROJ_FLAT, XRD_LAYOUT_MONO },
        { 2040, 1000, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR },
        { 2050, 1000, XRD_PROJ_FLAT, XRD_LAYOUT_MONO },
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        p = mk(rows[i].w, rows[i].h);
        d = resolve(&p, "x.mkv");
        expect(&d, rows[i].proj, rows[i].lay);
        assert_int_equal(d.proj_src, XRD_SRC_SHAPE);
        assert_int_equal(d.layout_src, XRD_SRC_SHAPE);
        assert_int_equal(d.packing, XRD_PACKING_AUTO);
        assert_int_equal(d.packing_src, XRD_SRC_AUTO);
    }
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
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
