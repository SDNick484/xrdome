#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "app/session.h"

#define TOL 1e-4

static xrd_detect detection(xrd_proj proj, xrd_layout layout)
{
    xrd_detect d;
    memset(&d, 0, sizeof d);
    d.proj = proj;
    d.layout = layout;
    d.packing = XRD_PACKING_AUTO;
    return d;
}

static xrd_filestate record(unsigned set)
{
    xrd_filestate r;
    memset(&r, 0, sizeof r);
    r.set = set;
    return r;
}

static void no_record_no_flags_flat(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    xrd_detect d = detection(XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
    xrd_session ss;
    xrd_session_start(&cfg, NULL, &d, &ss);
    assert_float_equal(ss.s.flat_zoom, 1.5f, TOL);
    assert_float_equal(ss.s.zoom, cfg.zoom_default, TOL);
    assert_int_equal((int)ss.explicit_flags, 0);
    assert_int_equal((int)ss.applied, 0);
    assert_int_equal(ss.s.proj, XRD_PROJ_FLAT);
    assert_int_equal(ss.s.layout, XRD_LAYOUT_MONO);
}

static void zoom_flag_goes_to_the_open_projection(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    cfg.zoom = 2.0f;
    cfg.explicit_flags = XRD_SET_ZOOM;
    xrd_session ss;

    xrd_detect flat = detection(XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
    xrd_session_start(&cfg, NULL, &flat, &ss);
    assert_float_equal(ss.s.flat_zoom, 2.0f, TOL);
    assert_float_equal(ss.s.zoom, cfg.zoom_default, TOL);
    assert_true(ss.explicit_flags & XRD_SET_FLAT_ZOOM);
    assert_false(ss.explicit_flags & XRD_SET_ZOOM);

    xrd_detect vr = detection(XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    xrd_session_start(&cfg, NULL, &vr, &ss);
    assert_float_equal(ss.s.zoom, 2.0f, TOL);
    assert_float_equal(ss.s.flat_zoom, 1.5f, TOL);
    assert_true(ss.explicit_flags & XRD_SET_ZOOM);
    assert_false(ss.explicit_flags & XRD_SET_FLAT_ZOOM);
}

static void zoom_flag_beats_record_for_open_projection_only(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    cfg.zoom = 2.0f;
    cfg.explicit_flags = XRD_SET_ZOOM;
    xrd_filestate rec = record(XRD_SET_ZOOM | XRD_SET_FLAT_ZOOM | XRD_SET_PROJ);
    rec.v.zoom = 0.56f;
    rec.v.flat_zoom = 1.1f;
    rec.v.proj = XRD_PROJ_FLAT;
    xrd_detect d = detection(XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
    xrd_session ss;
    xrd_session_start(&cfg, &rec, &d, &ss);
    assert_float_equal(ss.s.flat_zoom, 2.0f, TOL); /* the flag wins */
    assert_float_equal(ss.s.zoom, 0.56f, TOL);     /* the other projection keeps its record */
    assert_false(ss.applied & XRD_SET_FLAT_ZOOM);
    assert_true(ss.applied & XRD_SET_ZOOM);
}

static void record_hequirect_zoom(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    xrd_filestate rec = record(XRD_SET_PROJ | XRD_SET_ZOOM);
    rec.v.proj = XRD_PROJ_HEQUIRECT;
    rec.v.zoom = 0.56f;
    xrd_detect d = detection(XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    xrd_session ss;
    xrd_session_start(&cfg, &rec, &d, &ss);
    assert_float_equal(ss.s.zoom, 0.56f, TOL);
    assert_float_equal(ss.s.flat_zoom, 1.5f, TOL);
    assert_int_equal((int)ss.applied, XRD_SET_ZOOM | XRD_SET_PROJ);
}

static void record_does_not_override_detection(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    xrd_filestate rec = record(XRD_SET_PROJ | XRD_SET_LAYOUT | XRD_SET_PACKING | XRD_SET_ZOOM_MODE);
    rec.v.proj = XRD_PROJ_HEQUIRECT;
    rec.v.layout = XRD_LAYOUT_SBS_RL;
    rec.v.packing = XRD_PACKING_FULL;
    rec.v.zoom_mode = XRD_ZOOM_MAGNIFY;
    xrd_detect d = detection(XRD_PROJ_FLAT, XRD_LAYOUT_TB_LR);
    d.packing = XRD_PACKING_HALF;
    xrd_session ss;
    xrd_session_start(&cfg, &rec, &d, &ss);
    assert_int_equal(ss.s.proj, XRD_PROJ_FLAT);
    assert_int_equal(ss.s.layout, XRD_LAYOUT_TB_LR);
    assert_int_equal(ss.s.packing, XRD_PACKING_HALF);
    assert_int_equal(ss.s.zoom_mode, XRD_ZOOM_MAGNIFY); /* not a detection value */
}

static void zoom_mode_flag_beats_record(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    cfg.zoom_mode = XRD_ZOOM_SCREEN;
    cfg.explicit_flags = XRD_SET_ZOOM_MODE;
    xrd_filestate rec = record(XRD_SET_ZOOM_MODE);
    rec.v.zoom_mode = XRD_ZOOM_MAGNIFY;
    xrd_detect d = detection(XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
    xrd_session ss;
    xrd_session_start(&cfg, &rec, &d, &ss);
    assert_int_equal(ss.s.zoom_mode, XRD_ZOOM_SCREEN);
    assert_false(ss.applied & XRD_SET_ZOOM_MODE);
}

/* The reviewer's two cases (spec 8.1). */
static void toggle_keeps_the_unchosen_zoom_out_of_the_record(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    xrd_filestate rec = record(XRD_SET_PROJ | XRD_SET_ZOOM);
    rec.v.proj = XRD_PROJ_HEQUIRECT;
    rec.v.zoom = 0.56f;
    xrd_detect d = detection(XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    xrd_session ss;
    xrd_session_start(&cfg, &rec, &d, &ss);
    xrd_controls c;
    xrd_session_controls(&ss, &cfg, &c);
    xrd_controls_toggle_proj(&c);

    xrd_file_settings end;
    xrd_session_settings(&c, cfg.content_hfov, cfg.content_vfov, &end);
    xrd_filestate out;
    xrd_filestate_collect(&rec, ss.explicit_flags, &ss.s, &end, &out);
    assert_float_equal(out.v.zoom, 0.56f, TOL);
    assert_int_equal(out.v.proj, XRD_PROJ_FLAT);
    assert_true(out.set & XRD_SET_PROJ);
    assert_true(out.set & XRD_SET_ZOOM);
    assert_false(out.set & XRD_SET_FLAT_ZOOM);
}

static void toggle_alone_saves_only_proj(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    xrd_detect d = detection(XRD_PROJ_HEQUIRECT, XRD_LAYOUT_SBS_LR);
    xrd_session ss;
    xrd_session_start(&cfg, NULL, &d, &ss);
    xrd_controls c;
    xrd_session_controls(&ss, &cfg, &c);
    xrd_controls_toggle_proj(&c);

    xrd_file_settings end;
    xrd_session_settings(&c, cfg.content_hfov, cfg.content_vfov, &end);
    xrd_filestate out;
    xrd_filestate_collect(NULL, ss.explicit_flags, &ss.s, &end, &out);
    assert_int_equal((int)out.set, XRD_SET_PROJ);
    assert_int_equal(out.v.proj, XRD_PROJ_FLAT);
}

static void settings_take_zooms_by_projection(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    xrd_detect d = detection(XRD_PROJ_FLAT, XRD_LAYOUT_MONO);
    xrd_session ss;
    xrd_session_start(&cfg, NULL, &d, &ss);
    xrd_controls c;
    xrd_session_controls(&ss, &cfg, &c);
    assert_float_equal(c.zoom, 1.5f, TOL);
    assert_float_equal(c.zoom_other, cfg.zoom_default, TOL);
    assert_true(xrd_controls_screen_curve(&c) == 0.f); /* --screen-curve off by default */
    cfg.screen_curve = 1.5f;
    xrd_session_controls(&ss, &cfg, &c);
    assert_float_equal(xrd_controls_screen_curve(&c), 1.5f, TOL);
    xrd_file_settings s;
    xrd_session_settings(&c, 180.0f, 180.0f, &s);
    assert_float_equal(s.flat_zoom, 1.5f, TOL);
    assert_float_equal(s.zoom, cfg.zoom_default, TOL);
    xrd_controls_toggle_proj(&c);
    xrd_session_settings(&c, 180.0f, 180.0f, &s);
    assert_float_equal(s.flat_zoom, 1.5f, TOL);
    assert_float_equal(s.zoom, cfg.zoom_default, TOL);
    assert_int_equal(s.proj, XRD_PROJ_HEQUIRECT);
}

static void given_from_flags(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    xrd_detect_given flags, rec;
    xrd_session_given(&cfg, NULL, &flags, &rec);
    assert_false(flags.have_proj);
    assert_false(flags.have_layout);
    assert_false(flags.have_packing);
    assert_false(rec.have_proj);

    cfg.proj_auto = false;
    cfg.proj = XRD_PROJ_FLAT;
    cfg.packing = XRD_PACKING_FULL;
    cfg.layout_auto = false;
    cfg.layout = XRD_LAYOUT_TB_LR;
    xrd_session_given(&cfg, NULL, &flags, &rec);
    assert_true(flags.have_proj);
    assert_int_equal(flags.proj, XRD_PROJ_FLAT);
    assert_true(flags.have_layout);
    assert_int_equal(flags.layout, XRD_LAYOUT_TB_LR);
    assert_true(flags.have_packing);
    assert_int_equal(flags.packing, XRD_PACKING_FULL);

    cfg.layout_auto = true; /* --layout auto */
    xrd_session_given(&cfg, NULL, &flags, &rec);
    assert_false(flags.have_layout);
}

static void given_from_record(void **state)
{
    (void)state;
    xrd_config cfg;
    xrd_config_defaults(&cfg);
    xrd_detect_given flags, rec;

    xrd_filestate r = record(0); /* an old record: no proj, no layout */
    xrd_session_given(&cfg, &r, &flags, &rec);
    assert_true(rec.have_proj);
    assert_int_equal(rec.proj, XRD_PROJ_HEQUIRECT);
    assert_false(rec.have_layout);
    assert_false(rec.have_packing);

    r = record(XRD_SET_PROJ | XRD_SET_LAYOUT | XRD_SET_PACKING);
    r.v.proj = XRD_PROJ_FLAT;
    r.v.layout = XRD_LAYOUT_SBS_LR;
    r.v.packing = XRD_PACKING_HALF;
    xrd_session_given(&cfg, &r, &flags, &rec);
    assert_true(rec.have_proj);
    assert_int_equal(rec.proj, XRD_PROJ_FLAT);
    assert_true(rec.have_layout);
    assert_int_equal(rec.layout, XRD_LAYOUT_SBS_LR);
    assert_true(rec.have_packing);
    assert_int_equal(rec.packing, XRD_PACKING_HALF);
}

static xrd_config resume_cfg(bool resume)
{
    xrd_config c;
    xrd_config_defaults(&c);
    c.resume = resume;
    return c;
}

static void resume_start_needs_resume_and_a_position(void **state)
{
    (void)state;
    xrd_filestate rec = record(XRD_SET_POSITION);
    rec.v.position = 3723.0;
    double pos = 0.0;
    xrd_config c = resume_cfg(true);
    assert_true(xrd_session_resume_start(&c, &rec, &pos));
    assert_float_equal(pos, 3723.0, 1e-9);
    c = resume_cfg(false); /* off by default */
    assert_false(xrd_session_resume_start(&c, &rec, &pos));
    c = resume_cfg(true);
    assert_false(xrd_session_resume_start(&c, NULL, &pos));
    xrd_filestate none = record(XRD_SET_ZOOM);
    assert_false(xrd_session_resume_start(&c, &none, &pos));
}

static void resume_start_near_start_is_ignored(void **state)
{
    (void)state;
    xrd_config c = resume_cfg(true);
    xrd_filestate rec = record(XRD_SET_POSITION);
    double pos = 0.0;
    rec.v.position = 29.9;
    assert_false(xrd_session_resume_start(&c, &rec, &pos));
    rec.v.position = 30.0;
    assert_true(xrd_session_resume_start(&c, &rec, &pos));
}

static void resume_no_memory_beats_resume(void **state)
{
    (void)state;
    xrd_config c = resume_cfg(true);
    c.no_memory = true;
    xrd_filestate rec = record(XRD_SET_POSITION);
    rec.v.position = 100.0;
    double pos = 0.0;
    assert_false(xrd_session_resume_enabled(&c));
    assert_false(xrd_session_resume_start(&c, &rec, &pos));
    assert_int_equal(xrd_session_resume_save(&c, 100.0, 3600.0, &pos), -1);
}

static void resume_no_resume_beats_ini_resume(void **state)
{
    (void)state;
    /* xrd_config.resume is the merged ini and flags: --no-resume leaves false */
    xrd_config c = resume_cfg(false);
    xrd_filestate rec = record(XRD_SET_POSITION);
    rec.v.position = 100.0;
    double pos = 0.0;
    assert_false(xrd_session_resume_start(&c, &rec, &pos));
    assert_int_equal(xrd_session_resume_save(&c, 100.0, 3600.0, &pos), -1);
}

static void resume_explicit_start_wins(void **state)
{
    (void)state;
    xrd_config c = resume_cfg(true);
    xrd_filestate rec = record(XRD_SET_POSITION);
    rec.v.position = 100.0;
    double pos = 0.0;
    snprintf(c.mpv_opts[0], sizeof c.mpv_opts[0], "hwdec=no");
    c.mpv_opt_count = 1;
    assert_false(xrd_session_has_explicit_start(&c));
    assert_true(xrd_session_resume_start(&c, &rec, &pos));
    snprintf(c.mpv_opts[1], sizeof c.mpv_opts[1], "start=5");
    c.mpv_opt_count = 2;
    assert_true(xrd_session_has_explicit_start(&c));
    assert_false(xrd_session_resume_start(&c, &rec, &pos));
    /* a similar key is not an explicit start */
    snprintf(c.mpv_opts[1], sizeof c.mpv_opts[1], "start-time=5");
    assert_false(xrd_session_has_explicit_start(&c));
    /* an explicit start still lets the position at quit be saved */
    snprintf(c.mpv_opts[1], sizeof c.mpv_opts[1], "start=5");
    assert_int_equal(xrd_session_resume_save(&c, 100.0, 3600.0, &pos), 1);
}

static void resume_save_rules(void **state)
{
    (void)state;
    xrd_config c = resume_cfg(true);
    double pos = -1.0;
    assert_int_equal(xrd_session_resume_save(&c, 100.5, 3600.0, &pos), 1);
    assert_float_equal(pos, 100.5, 1e-9);
    assert_int_equal(xrd_session_resume_save(&c, 10.0, 3600.0, &pos), 0);   /* first 30 s */
    assert_int_equal(xrd_session_resume_save(&c, 3560.0, 3600.0, &pos), 0); /* last 60 s */
    assert_int_equal(xrd_session_resume_save(&c, 3539.0, 3600.0, &pos), 1);
    assert_int_equal(xrd_session_resume_save(&c, 100.0, -1.0, &pos), -1);   /* unknown duration */
    assert_int_equal(xrd_session_resume_save(&c, -1.0, 3600.0, &pos), -1);  /* unknown position */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(no_record_no_flags_flat),
        cmocka_unit_test(zoom_flag_goes_to_the_open_projection),
        cmocka_unit_test(zoom_flag_beats_record_for_open_projection_only),
        cmocka_unit_test(record_hequirect_zoom),
        cmocka_unit_test(record_does_not_override_detection),
        cmocka_unit_test(zoom_mode_flag_beats_record),
        cmocka_unit_test(toggle_keeps_the_unchosen_zoom_out_of_the_record),
        cmocka_unit_test(toggle_alone_saves_only_proj),
        cmocka_unit_test(settings_take_zooms_by_projection),
        cmocka_unit_test(given_from_flags),
        cmocka_unit_test(given_from_record),
        cmocka_unit_test(resume_start_needs_resume_and_a_position),
        cmocka_unit_test(resume_start_near_start_is_ignored),
        cmocka_unit_test(resume_no_memory_beats_resume),
        cmocka_unit_test(resume_no_resume_beats_ini_resume),
        cmocka_unit_test(resume_explicit_start_wins),
        cmocka_unit_test(resume_save_rules),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
