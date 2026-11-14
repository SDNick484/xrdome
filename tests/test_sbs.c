#define _POSIX_C_SOURCE 200809L /* mkdtemp, setenv */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

#include "display/output.h"
#include "display/sbs.h"
#include "driver/xrdriver.h"

#define NOW 1794669614LL

static xrd_xrdriver_state state(bool hb, long long age, bool have_sbs, bool sup, bool en)
{
    xrd_xrdriver_state st = {.have_heartbeat = hb, .heartbeat = NOW - age, .have_sbs = have_sbs,
                             .sbs_supported = sup, .sbs_enabled = en};
    return st;
}

static void decide(void **s)
{
    (void)s;
    xrd_xrdriver_state st;
    assert_int_equal(xrd_sbs_decide(NULL, NOW), XRD_SBS_NO_DRIVER);
    st = state(true, 1, true, true, false);
    assert_int_equal(xrd_sbs_decide(&st, NOW), XRD_SBS_GO);
    st = state(true, 600, true, true, false); /* the driver crashed; its file stayed */
    assert_int_equal(xrd_sbs_decide(&st, NOW), XRD_SBS_STALE);
    st = state(false, 0, true, true, false);
    assert_int_equal(xrd_sbs_decide(&st, NOW), XRD_SBS_STALE);
    st = state(true, 0, false, false, false);
    assert_int_equal(xrd_sbs_decide(&st, NOW), XRD_SBS_NO_GLASSES);
    st = state(true, 0, true, false, false);
    assert_int_equal(xrd_sbs_decide(&st, NOW), XRD_SBS_UNSUPPORTED);
    st = state(true, 0, true, true, true);
    assert_int_equal(xrd_sbs_decide(&st, NOW), XRD_SBS_ALREADY_ON);
    /* Staleness wins over what a dead driver last wrote. */
    st = state(true, 600, true, true, true);
    assert_int_equal(xrd_sbs_decide(&st, NOW), XRD_SBS_STALE);
}

static void reasons(void **s)
{
    (void)s;
    char buf[512];
    xrd_xrdriver_state st = state(true, 412, true, true, false);
    xrd_sbs_reason(XRD_SBS_STALE, &st, NOW, "/x/xr_driver_state", buf, sizeof buf);
    assert_string_equal(buf,
                        "XRLinuxDriver is not running (the heartbeat in /x/xr_driver_state is 412 s old)");
    xrd_sbs_reason(XRD_SBS_NO_DRIVER, NULL, NOW, "/x/xr_driver_state", buf, sizeof buf);
    assert_string_equal(buf, "XRLinuxDriver is not running (cannot read /x/xr_driver_state)");
    xrd_sbs_reason(XRD_SBS_NO_GLASSES, &st, NOW, "/x", buf, sizeof buf);
    assert_non_null(strstr(buf, "no glasses"));
    xrd_sbs_reason(XRD_SBS_UNSUPPORTED, &st, NOW, "/x", buf, sizeof buf);
    assert_non_null(strstr(buf, "cannot switch to SBS"));
    xrd_sbs_reason(XRD_SBS_ALREADY_ON, &st, NOW, "/x", buf, sizeof buf);
    assert_non_null(strstr(buf, "already in SBS"));
    char small[8];
    xrd_sbs_reason(XRD_SBS_STALE, &st, NOW, "/x", small, sizeof small);
    assert_int_equal(strlen(small), sizeof small - 1);
}

static void set_out(xrd_output_info *o, const char *name, int x, int w, int h)
{
    memset(o, 0, sizeof *o);
    snprintf(o->name, sizeof o->name, "%s", name);
    o->x = x;
    o->w = w;
    o->h = h;
}

static void candidate(void **s)
{
    (void)s;
    xrd_output_info o[2];
    set_out(&o[0], "eDP-1 14\"", 0, 1920, 1200);
    set_out(&o[1], "VITURE 23\"", 1920, 1920, 1080);
    int idx;
    int sel = xrd_output_select(o, 2, "auto", &idx);
    assert_int_equal(sel, XRD_OUT_NOT_FOUND);
    assert_true(xrd_output_sbs_candidate(o, 2, "auto", sel));
    assert_true(xrd_output_sbs_candidate(o, 2, NULL, sel));
    /* A named output in 1080p qualifies. */
    sel = xrd_output_select(o, 2, "VITURE", &idx);
    assert_int_equal(sel, XRD_OUT_NOT_SBS);
    assert_true(xrd_output_sbs_candidate(o, 2, "VITURE", sel));
    /* A named output of another size (the laptop panel) does not. */
    sel = xrd_output_select(o, 2, "eDP-1", &idx);
    assert_int_equal(sel, XRD_OUT_NOT_SBS);
    assert_false(xrd_output_sbs_candidate(o, 2, "eDP-1", sel));
    /* Nor does a name that is not there. */
    sel = xrd_output_select(o, 2, "HDMI-9", &idx);
    assert_false(xrd_output_sbs_candidate(o, 2, "HDMI-9", sel));
    /* Auto with no outputs at all: the glasses may still be switchable. */
    assert_true(xrd_output_sbs_candidate(NULL, 0, "auto", XRD_OUT_NOT_FOUND));
    assert_false(xrd_output_sbs_candidate(o, 2, "auto", XRD_OUT_OK));
}

/* ---- fake display and clocks ---- */

typedef struct {
    uint64_t ms;         /* monotonic clock, advanced by wait */
    int waits, enums;
    int sbs_after;       /* enumerate shows the SBS output from this call on; <0: never */
    int fail_until;      /* enumerate fails on calls before this one */
    int quit_on_wait;    /* wait returns false on this call (1-based); 0: never */
    const char *name;    /* name of the glasses' output */
    char dir[64];
} fake;

static int fake_enumerate(void *ctx, xrd_output_info *outs, int max)
{
    fake *f = ctx;
    int call = f->enums++;
    if (call < f->fail_until)
        return -1;
    assert_true(max >= 2);
    set_out(&outs[0], "eDP-1 14\"", 0, 1920, 1200);
    bool sbs = f->sbs_after >= 0 && call >= f->sbs_after;
    set_out(&outs[1], f->name, 1920, sbs ? 3840 : 1920, 1080);
    return 2;
}

static bool fake_wait(void *ctx, int ms)
{
    fake *f = ctx;
    assert_true(ms > 0 && ms <= XRD_SBS_POLL_MS);
    f->waits++;
    f->ms += (uint64_t)ms;
    return f->quit_on_wait == 0 || f->waits != f->quit_on_wait;
}

static uint64_t fake_now_ms(void *ctx)
{
    return ((fake *)ctx)->ms;
}

static long long fake_epoch(void *ctx)
{
    (void)ctx;
    return NOW;
}

static const xrd_sbs_ops FAKE_OPS = {fake_enumerate, fake_wait, fake_now_ms, fake_epoch};

static void write_file(const char *dir, const char *name, const char *text)
{
    char path[128];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    assert_non_null(f);
    fputs(text, f);
    fclose(f);
}

/* The control file's text, or "" when there is none. */
static const char *control(const fake *f)
{
    static char buf[128];
    char path[128];
    snprintf(path, sizeof path, "%s/%s", f->dir, XRD_XRDRIVER_CONTROL);
    buf[0] = '\0';
    FILE *fp = fopen(path, "r");
    if (fp) {
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = '\0';
        fclose(fp);
    }
    return buf;
}

static void write_state(const fake *f, long long age, const char *sbs_lines)
{
    char text[256];
    snprintf(text, sizeof text, "heartbeat=%lld\nhardware_id=ab\n%s", NOW - age, sbs_lines);
    write_file(f->dir, XRD_XRDRIVER_STATE, text);
}

#define MIRROR "sbs_mode_supported=true\nsbs_mode_enabled=false\n"

/* Every test with files gets its own driver directory, so nothing reaches
 * a real /dev/shm/xr_driver_control. */
static int setup(void **s)
{
    fake *f = calloc(1, sizeof *f);
    assert_non_null(f);
    snprintf(f->dir, sizeof f->dir, "/tmp/xrd_sbs_XXXXXX");
    assert_non_null(mkdtemp(f->dir));
    setenv(XRD_XRDRIVER_DIR_ENV, f->dir, 1);
    f->name = "VITURE 23\"";
    f->sbs_after = -1;
    *s = f;
    return 0;
}

static int teardown(void **s)
{
    fake *f = *s;
    char path[128];
    snprintf(path, sizeof path, "%s/%s", f->dir, XRD_XRDRIVER_CONTROL);
    unlink(path);
    snprintf(path, sizeof path, "%s/%s", f->dir, XRD_XRDRIVER_STATE);
    unlink(path);
    rmdir(f->dir);
    setenv(XRD_XRDRIVER_DIR_ENV, "/nonexistent/xrdome-test", 1);
    free(f);
    return 0;
}

/* The outputs xrdome found at start: the glasses in 1080p. */
static int start_outputs(fake *f, xrd_output_info *outs, int *n)
{
    *n = fake_enumerate(f, outs, 4);
    f->enums = 0;
    return xrd_output_select(outs, *n, "auto", &(int){0});
}

static void switch_works(void **s)
{
    fake *f = *s;
    write_state(f, 1, MIRROR);
    f->sbs_after = 3; /* the fourth listing shows 3840x1080 */
    xrd_output_info outs[4];
    int n, idx = -1;
    assert_int_equal(start_outputs(f, outs, &n), XRD_OUT_NOT_FOUND);
    xrd_sbs_result r;
    int sel = xrd_sbs_switch(&FAKE_OPS, f, "auto", 8000, outs, 4, &n, &idx, &r);
    assert_int_equal(sel, XRD_OUT_OK);
    assert_int_equal(r.outcome, XRD_SBS_SWITCHED);
    assert_int_equal(idx, 1);
    assert_int_equal(outs[1].w, 3840);
    assert_int_equal(f->waits, 4); /* waited before each listing */
    assert_int_equal((int)r.waited_ms, 4 * XRD_SBS_POLL_MS);
    assert_string_equal(control(f), "sbs_mode=enable\n");
}

static void switch_named_output(void **s)
{
    fake *f = *s;
    write_state(f, 0, MIRROR);
    f->sbs_after = 1;
    xrd_output_info outs[4];
    int n, idx = -1;
    n = fake_enumerate(f, outs, 4);
    f->enums = 0;
    int sel0 = xrd_output_select(outs, n, "VITURE", &idx);
    assert_int_equal(sel0, XRD_OUT_NOT_SBS);
    assert_true(xrd_output_sbs_candidate(outs, n, "VITURE", sel0));
    xrd_sbs_result r;
    assert_int_equal(xrd_sbs_switch(&FAKE_OPS, f, "VITURE", 8000, outs, 4, &n, &idx, &r),
                     XRD_OUT_OK);
    assert_int_equal(idx, 1);
    assert_string_equal(control(f), "sbs_mode=enable\n");
}

static void switch_times_out(void **s)
{
    fake *f = *s;
    write_state(f, 0, MIRROR);
    xrd_output_info outs[4];
    int n, idx = 77;
    start_outputs(f, outs, &n);
    xrd_sbs_result r;
    int sel = xrd_sbs_switch(&FAKE_OPS, f, "auto", 1000, outs, 4, &n, &idx, &r);
    assert_int_equal(sel, XRD_OUT_NOT_FOUND);
    assert_int_equal(r.outcome, XRD_SBS_TIMED_OUT);
    assert_int_equal(idx, 77);
    assert_int_equal(f->ms, 1000); /* waited the whole timeout, and no longer */
    assert_int_equal(f->enums, 1000 / XRD_SBS_POLL_MS);
    assert_string_equal(r.why, "asked XRLinuxDriver to switch the glasses to SBS, but no "
                               "3840x1080 output appeared within 1 s");
    assert_string_equal(control(f), "sbs_mode=enable\n");
    /* The last listing is kept for the outputs: line. */
    assert_int_equal(n, 2);
}

static void switch_odd_timeout(void **s)
{
    fake *f = *s;
    write_state(f, 0, MIRROR);
    xrd_output_info outs[4];
    int n, idx;
    start_outputs(f, outs, &n);
    xrd_sbs_result r;
    xrd_sbs_switch(&FAKE_OPS, f, "auto", 450, outs, 4, &n, &idx, &r);
    assert_int_equal(r.outcome, XRD_SBS_TIMED_OUT);
    assert_int_equal(f->ms, 450); /* 200 + 200 + 50: the last wait is cut to the deadline */
    assert_int_equal(f->enums, 3);
}

static void switch_retries_failed_listing(void **s)
{
    fake *f = *s;
    write_state(f, 0, MIRROR);
    xrd_output_info outs[4];
    int n, idx;
    start_outputs(f, outs, &n);
    f->sbs_after = 0;
    f->fail_until = 2; /* the first two listings fail */
    xrd_sbs_result r;
    assert_int_equal(xrd_sbs_switch(&FAKE_OPS, f, "auto", 8000, outs, 4, &n, &idx, &r),
                     XRD_OUT_OK);
    assert_int_equal(f->enums, 3);
}

static void switch_quit_while_waiting(void **s)
{
    fake *f = *s;
    write_state(f, 0, MIRROR);
    f->quit_on_wait = 2;
    xrd_output_info outs[4];
    int n, idx;
    start_outputs(f, outs, &n);
    xrd_sbs_result r;
    assert_int_equal(xrd_sbs_switch(&FAKE_OPS, f, "auto", 8000, outs, 4, &n, &idx, &r),
                     XRD_OUT_NOT_FOUND);
    assert_int_equal(r.outcome, XRD_SBS_QUIT);
    assert_int_equal(f->waits, 2);
}

/* Each state that must not ask: nothing is written, nothing is waited for. */
static void not_asked(void **s)
{
    fake *f = *s;
    struct { long long age; const char *sbs; const char *why; bool write; } cases[] = {
        {0, "", "cannot read", false},
        {600, MIRROR, "is 600 s old", true},
        {0, "", "no glasses", true},
        {0, "sbs_mode_supported=false\nsbs_mode_enabled=false\n", "cannot switch", true},
        {0, "sbs_mode_supported=true\nsbs_mode_enabled=true\n", "already in SBS", true},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (cases[i].write)
            write_state(f, cases[i].age, cases[i].sbs);
        f->waits = f->enums = 0;
        xrd_output_info outs[4];
        int n, idx = 77;
        start_outputs(f, outs, &n);
        xrd_sbs_result r;
        int sel = xrd_sbs_switch(&FAKE_OPS, f, "auto", 8000, outs, 4, &n, &idx, &r);
        assert_int_equal(sel, XRD_OUT_NOT_FOUND);
        assert_int_equal(r.outcome, XRD_SBS_NOT_ASKED);
        assert_non_null(strstr(r.why, cases[i].why));
        assert_int_equal(f->waits, 0);
        assert_int_equal(f->enums, 0);
        assert_int_equal(idx, 77);
        assert_string_equal(control(f), "");
    }
}

static void restore(void **s)
{
    fake *f = *s;
    char why[512] = "";
    /* No state file: nothing written. */
    assert_false(xrd_sbs_restore(NOW, why, sizeof why));
    assert_non_null(strstr(why, "not running"));
    assert_string_equal(control(f), "");
    /* A stale driver: nothing written. */
    write_state(f, 60, "sbs_mode_supported=true\nsbs_mode_enabled=true\n");
    assert_false(xrd_sbs_restore(NOW, why, sizeof why));
    assert_non_null(strstr(why, "60 s old"));
    assert_string_equal(control(f), "");
    /* Running: the disable request. */
    write_state(f, 0, "sbs_mode_supported=true\nsbs_mode_enabled=true\n");
    assert_true(xrd_sbs_restore(NOW, why, sizeof why));
    assert_string_equal(control(f), "sbs_mode=disable\n");
}

int main(void)
{
    /* Belt and braces: even a test without the fixture never sees /dev/shm. */
    setenv(XRD_XRDRIVER_DIR_ENV, "/nonexistent/xrdome-test", 1);
    const struct CMUnitTest t[] = {
        cmocka_unit_test(decide),
        cmocka_unit_test(reasons),
        cmocka_unit_test(candidate),
        cmocka_unit_test_setup_teardown(switch_works, setup, teardown),
        cmocka_unit_test_setup_teardown(switch_named_output, setup, teardown),
        cmocka_unit_test_setup_teardown(switch_times_out, setup, teardown),
        cmocka_unit_test_setup_teardown(switch_odd_timeout, setup, teardown),
        cmocka_unit_test_setup_teardown(switch_retries_failed_listing, setup, teardown),
        cmocka_unit_test_setup_teardown(switch_quit_while_waiting, setup, teardown),
        cmocka_unit_test_setup_teardown(not_asked, setup, teardown),
        cmocka_unit_test_setup_teardown(restore, setup, teardown),
    };
    return cmocka_run_group_tests(t, NULL, NULL);
}
