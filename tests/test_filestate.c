#define _POSIX_C_SOURCE 200809L /* mkdtemp, setenv, symlink */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <cmocka.h>

#include "store/filestate.h"

static char g_dir[64];
static char *g_old_xdg, *g_old_home;

static void rm_tree(void)
{
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
    if (system(cmd) != 0) { /* best effort */ }
}

static int setup(void **state)
{
    (void)state;
    strcpy(g_dir, "/tmp/xrd_fs_XXXXXX");
    assert_non_null(mkdtemp(g_dir));
    const char *x = getenv("XDG_STATE_HOME"), *h = getenv("HOME");
    g_old_xdg = x ? strdup(x) : NULL;
    g_old_home = h ? strdup(h) : NULL;
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    rm_tree();
    if (g_old_xdg) setenv("XDG_STATE_HOME", g_old_xdg, 1); else unsetenv("XDG_STATE_HOME");
    if (g_old_home) setenv("HOME", g_old_home, 1); else unsetenv("HOME");
    free(g_old_xdg);
    free(g_old_home);
    g_old_xdg = g_old_home = NULL;
    return 0;
}

static void write_file(char *out, size_t outlen, const char *name, const void *bytes, size_t len)
{
    snprintf(out, outlen, "%s/%s", g_dir, name);
    FILE *f = fopen(out, "wb");
    assert_non_null(f);
    assert_int_equal((int)fwrite(bytes, 1, len, f), (int)len);
    fclose(f);
}

static unsigned char *pattern(size_t n)
{
    unsigned char *b = malloc(n ? n : 1);
    assert_non_null(b);
    for (size_t i = 0; i < n; i++)
        b[i] = (unsigned char)((i * 7 + 3) & 0xff);
    return b;
}

static const char *key_of(const char *path)
{
    static char key[XRD_FILESTATE_KEY_MAX];
    assert_int_equal(xrd_filestate_key(path, key, sizeof key), 0);
    return key;
}

static void make_pattern_file(char *path, size_t pathlen, const char *name, size_t n)
{
    unsigned char *b = pattern(n);
    write_file(path, pathlen, name, b, n);
    free(b);
}

static void flip_byte(const char *path, long off)
{
    FILE *f = fopen(path, "r+b");
    assert_non_null(f);
    assert_int_equal(fseek(f, off, SEEK_SET), 0);
    int c = fgetc(f);
    assert_true(c != EOF);
    assert_int_equal(fseek(f, off, SEEK_SET), 0);
    fputc(c ^ 0xff, f);
    fclose(f);
}

static void fnv_vectors(void **state)
{
    (void)state;
    assert_true(xrd_fnv1a64(XRD_FNV1A64_INIT, "", 0) == 0xcbf29ce484222325ULL);
    assert_true(xrd_fnv1a64(XRD_FNV1A64_INIT, "a", 1) == 0xaf63dc4c8601ec8cULL);
    assert_true(xrd_fnv1a64(XRD_FNV1A64_INIT, "foobar", 6) == 0x85944171f73967e8ULL);
}

static void key_small_files(void **state)
{
    (void)state;
    char p[128];
    write_file(p, sizeof p, "empty", "", 0);
    assert_string_equal(key_of(p), "0-cbf29ce484222325");
    write_file(p, sizeof p, "one", "a", 1);
    assert_string_equal(key_of(p), "1-089c4307b54596b7");
}

static void key_overlap_and_large(void **state)
{
    (void)state;
    char p[128];
    make_pattern_file(p, sizeof p, "f100000", 100000);
    assert_string_equal(key_of(p), "100000-09f9b79371c62325");
    make_pattern_file(p, sizeof p, "f200000", 200000);
    assert_string_equal(key_of(p), "200000-8ae86f2097da2325");
}

static void key_survives_rename(void **state)
{
    (void)state;
    char p[128], q[128];
    make_pattern_file(p, sizeof p, "orig", 200000);
    char before[XRD_FILESTATE_KEY_MAX];
    strcpy(before, key_of(p));
    snprintf(q, sizeof q, "%s/renamed", g_dir);
    assert_int_equal(rename(p, q), 0);
    assert_string_equal(key_of(q), before);
}

static void key_changes_on_edit(void **state)
{
    (void)state;
    char p[128];
    make_pattern_file(p, sizeof p, "edit", 200000);
    char base[XRD_FILESTATE_KEY_MAX];
    strcpy(base, key_of(p));

    flip_byte(p, 10);
    assert_string_not_equal(key_of(p), base);
    flip_byte(p, 10);
    assert_string_equal(key_of(p), base);

    flip_byte(p, 199990);
    assert_string_not_equal(key_of(p), base);
    flip_byte(p, 199990);

    flip_byte(p, 100000); /* outside the first and last 65536 bytes */
    assert_string_equal(key_of(p), base);
}

static void key_rejects_non_files(void **state)
{
    (void)state;
    char key[XRD_FILESTATE_KEY_MAX], p[128];
    assert_int_equal(xrd_filestate_key(g_dir, key, sizeof key), -1);
    snprintf(p, sizeof p, "%s/missing", g_dir);
    assert_int_equal(xrd_filestate_key(p, key, sizeof key), -1);
}

static void key_follows_symlink(void **state)
{
    (void)state;
    char p[128], l[128];
    make_pattern_file(p, sizeof p, "target", 200000);
    snprintf(l, sizeof l, "%s/link", g_dir);
    assert_int_equal(symlink(p, l), 0);
    assert_string_equal(key_of(l), key_of(p));
}

static void key_buffer_too_small(void **state)
{
    (void)state;
    char p[128], key[8];
    make_pattern_file(p, sizeof p, "small", 100);
    assert_int_equal(xrd_filestate_key(p, key, 8), -1);
}

static void dir_selection(void **state)
{
    (void)state;
    char d[256];

    setenv("XDG_STATE_HOME", "/x/state", 1);
    setenv("HOME", "/h/u", 1);
    assert_int_equal(xrd_filestate_dir(d, sizeof d), 0);
    assert_string_equal(d, "/x/state/xrdome/files");

    unsetenv("XDG_STATE_HOME");
    assert_int_equal(xrd_filestate_dir(d, sizeof d), 0);
    assert_string_equal(d, "/h/u/.local/state/xrdome/files");

    setenv("XDG_STATE_HOME", "", 1);
    assert_int_equal(xrd_filestate_dir(d, sizeof d), 0);
    assert_string_equal(d, "/h/u/.local/state/xrdome/files");

    setenv("XDG_STATE_HOME", "rel/path", 1);
    assert_int_equal(xrd_filestate_dir(d, sizeof d), 0);
    assert_string_equal(d, "/h/u/.local/state/xrdome/files");

    setenv("HOME", "", 1);
    assert_int_equal(xrd_filestate_dir(d, sizeof d), -1);

    unsetenv("XDG_STATE_HOME");
    unsetenv("HOME");
    assert_int_equal(xrd_filestate_dir(d, sizeof d), -1);

    setenv("XDG_STATE_HOME", "/x/state", 1);
    assert_int_equal(xrd_filestate_dir(d, 8), -1);
}


static xrd_filestate make_state(unsigned set)
{
    xrd_filestate st;
    memset(&st, 0, sizeof st);
    st.set = set;
    st.v.zoom = 0.7513f;
    st.v.layout = XRD_LAYOUT_SBS_RL;
    st.v.swap = true;
    st.v.content_hfov = 120.0f;
    st.v.content_vfov = 90.0f;
    st.v.convergence = 1.5f;
    return st;
}

static void format_only_set_fields(void **state)
{
    (void)state;
    xrd_filestate st = make_state(XRD_SET_ZOOM | XRD_SET_CONTENT_FOV);
    char buf[512];
    int n = xrd_filestate_format(&st, "/v/a.mp4", "2026-11-12", buf, sizeof buf);
    const char *want =
        "# xrdome per-file settings\nversion = 1\npath = /v/a.mp4\nsaved = 2026-11-12\n"
        "proj = hequirect\nzoom = 0.7513\ncontent_fov = 120x90\n";
    assert_string_equal(buf, want);
    assert_int_equal(n, (int)strlen(want));
}

static void format_all_fields(void **state)
{
    (void)state;
    xrd_filestate st = make_state(XRD_SET_ZOOM | XRD_SET_LAYOUT | XRD_SET_SWAP |
                                  XRD_SET_CONTENT_FOV | XRD_SET_CONVERGENCE);
    char buf[512];
    assert_true(xrd_filestate_format(&st, "/v/a.mp4", "2026-11-12", buf, sizeof buf) > 0);
    assert_string_equal(buf,
        "# xrdome per-file settings\nversion = 1\npath = /v/a.mp4\nsaved = 2026-11-12\n"
        "proj = hequirect\nzoom = 0.7513\nlayout = sbs-rl\nswap = yes\n"
        "content_fov = 120x90\nconvergence = 1.50\n");
}

static void format_too_small(void **state)
{
    (void)state;
    xrd_filestate st = make_state(XRD_SET_ZOOM);
    char buf[16];
    assert_int_equal(xrd_filestate_format(&st, "/v/a.mp4", "2026-11-12", buf, sizeof buf), -1);
}

static void round_trip(void **state)
{
    (void)state;
    xrd_filestate st = make_state(XRD_SET_ZOOM | XRD_SET_LAYOUT | XRD_SET_SWAP |
                                  XRD_SET_CONTENT_FOV | XRD_SET_CONVERGENCE);
    char buf[512], err[128];
    assert_true(xrd_filestate_format(&st, "/v/a.mp4", "2026-11-12", buf, sizeof buf) > 0);
    xrd_filestate out;
    memset(&out, 0, sizeof out);
    assert_int_equal(xrd_filestate_parse(buf, &out, err, sizeof err), XRD_FS_OK);
    assert_int_equal((int)out.set, (int)st.set);
    assert_true(fabsf(out.v.zoom - st.v.zoom) < 1e-4f);
    assert_int_equal(out.v.layout, st.v.layout);
    assert_true(out.v.swap == st.v.swap);
    assert_true(fabsf(out.v.content_hfov - st.v.content_hfov) < 1e-4f);
    assert_true(fabsf(out.v.content_vfov - st.v.content_vfov) < 1e-4f);
    assert_true(fabsf(out.v.convergence - st.v.convergence) < 1e-4f);
}

static void parse_minimal(void **state)
{
    (void)state;
    xrd_filestate out;
    char err[128];
    assert_int_equal(xrd_filestate_parse("version = 1\n", &out, err, sizeof err), XRD_FS_OK);
    assert_int_equal((int)out.set, 0);
}

static void parse_tolerates(void **state)
{
    (void)state;
    xrd_filestate out;
    char err[128];
    const char *t = "# comment\r\n\r\nversion = 1   \r\nfuture_key = 7\r\n  zoom=0.5  \r\n"
                    "path = ignored\nsaved = whenever\nproj = hequirect\n";
    assert_int_equal(xrd_filestate_parse(t, &out, err, sizeof err), XRD_FS_OK);
    assert_int_equal((int)out.set, (int)XRD_SET_ZOOM);
    assert_true(fabsf(out.v.zoom - 0.5f) < 1e-6f);
}

static void parse_duplicate_last_wins(void **state)
{
    (void)state;
    xrd_filestate out;
    char err[128];
    assert_int_equal(xrd_filestate_parse("version = 1\nzoom = 0.5\nzoom = 0.8\n", &out, err,
                                         sizeof err), XRD_FS_OK);
    assert_true(fabsf(out.v.zoom - 0.8f) < 1e-6f);
}

static void parse_bad(void **state)
{
    (void)state;
    static const char *const cases[] = {
        "zoom = 1\n", "version = x\n", "version = 1\nzoom = 9\n", "version = 1\nzoom = abc\n",
        "version = 1\nlayout = tb\n", "version = 1\nswap = maybe\n",
        "version = 1\ncontent_fov = 200x90\n", "version = 1\ncontent_fov = 120\n",
        "version = 1\nconvergence = 6\n", "version = 1\njust some words\n",
        "version = 0\n", "version = -1\n",
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        xrd_filestate out = make_state(XRD_SET_ZOOM);
        char err[128] = "";
        assert_int_equal(xrd_filestate_parse(cases[i], &out, err, sizeof err), XRD_FS_BAD);
        assert_int_equal((int)out.set, 0);
        assert_non_null(strstr(err, "line"));
    }
}

static void parse_bad_reports_line(void **state)
{
    (void)state;
    xrd_filestate out;
    char err[128];
    assert_int_equal(xrd_filestate_parse("version = 1\n\nzoom = 9\nlayout = tb\n", &out, err,
                                         sizeof err), XRD_FS_BAD);
    assert_non_null(strstr(err, "line 3"));
}

static void parse_unsupported(void **state)
{
    (void)state;
    xrd_filestate out = make_state(XRD_SET_ZOOM);
    char err[128];
    assert_int_equal(xrd_filestate_parse("version = 2\n", &out, err, sizeof err),
                     XRD_FS_UNSUPPORTED);
    assert_int_equal((int)out.set, 0);
    out = make_state(XRD_SET_ZOOM);
    assert_int_equal(xrd_filestate_parse("version = 1\nproj = flat\n", &out, err, sizeof err),
                     XRD_FS_UNSUPPORTED);
    assert_int_equal((int)out.set, 0);
    assert_int_equal(xrd_filestate_parse("version = 2\nzoom = abc\n", &out, err, sizeof err),
                     XRD_FS_UNSUPPORTED);
    assert_int_equal(xrd_filestate_parse("zoom = abc\nversion = 2\n", &out, err, sizeof err),
                     XRD_FS_UNSUPPORTED);
}

static void path_label_cannot_inject(void **state)
{
    (void)state;
    xrd_filestate st;
    memset(&st, 0, sizeof st);
    char buf[512], err[128];
    assert_true(xrd_filestate_format(&st, "/v/a\nzoom = 4.mp4", "2026-11-12", buf, sizeof buf) > 0);
    int nl = 0;
    for (const char *p = buf; *p; p++)
        nl += *p == '\n';
    assert_int_equal(nl, 5); /* comment, version, path, saved, proj */
    assert_non_null(strstr(buf, "path = /v/a?zoom = 4.mp4\n"));
    xrd_filestate out;
    assert_int_equal(xrd_filestate_parse(buf, &out, err, sizeof err), XRD_FS_OK);
    assert_int_equal((int)out.set, 0);
}

static void record_path(void **state)
{
    (void)state;
    char b[64];
    assert_int_equal(xrd_filestate_record_path("/d", "12-ab", b, sizeof b), 0);
    assert_string_equal(b, "/d/12-ab.ini");
    assert_int_equal(xrd_filestate_record_path("/d", "12-ab", b, 8), -1);
}

static int count_entries(const char *dir, char *first, size_t firstlen)
{
    DIR *d = opendir(dir);
    assert_non_null(d);
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (n++ == 0)
            snprintf(first, firstlen, "%s", e->d_name);
    }
    closedir(d);
    return n;
}

static void save_load_delete(void **state)
{
    (void)state;
    char dir[160], rec[200], err[128] = "", first[128] = "", key[XRD_FILESTATE_KEY_MAX] = "abc-0123";
    snprintf(dir, sizeof dir, "%s/s/xrdome/files", g_dir);
    xrd_filestate st = make_state(XRD_SET_ZOOM | XRD_SET_LAYOUT | XRD_SET_CONTENT_FOV);
    assert_int_equal(xrd_filestate_save(dir, key, &st, "/v/a.mp4", err, sizeof err), 0);

    struct stat sb;
    assert_int_equal(stat(dir, &sb), 0);
    assert_true(S_ISDIR(sb.st_mode));
    assert_int_equal((int)(sb.st_mode & 07777), 0700);
    assert_int_equal(count_entries(dir, first, sizeof first), 1);
    assert_string_equal(first, "abc-0123.ini");

    assert_int_equal(xrd_filestate_record_path(dir, key, rec, sizeof rec), 0);
    xrd_filestate out;
    assert_int_equal(xrd_filestate_load(rec, &out, err, sizeof err), XRD_FS_OK);
    assert_int_equal((int)out.set, (int)st.set);
    assert_true(fabsf(out.v.zoom - 0.7513f) < 1e-4f);
    assert_int_equal(out.v.layout, XRD_LAYOUT_SBS_RL);
    assert_true(fabsf(out.v.content_hfov - 120.0f) < 1e-4f);

    assert_int_equal(xrd_filestate_save(dir, key, &st, "/v/a.mp4", err, sizeof err), 0);
    assert_int_equal(count_entries(dir, first, sizeof first), 1);

    assert_int_equal(xrd_filestate_delete(dir, key), 1);
    assert_int_equal(xrd_filestate_delete(dir, key), 0);
    assert_int_equal(xrd_filestate_load(rec, &out, err, sizeof err), XRD_FS_NONE);
}

static void save_relative_path_label(void **state)
{
    (void)state;
    char dir[160], rec[200], err[128], want[256], cwd[256];
    assert_non_null(getcwd(cwd, sizeof cwd));
    assert_int_equal(chdir(g_dir), 0);
    snprintf(dir, sizeof dir, "%s/files", g_dir);
    xrd_filestate st = make_state(XRD_SET_ZOOM);
    int rc = xrd_filestate_save(dir, "k-1", &st, "clip.mp4", err, sizeof err);
    assert_int_equal(chdir(cwd), 0);
    assert_int_equal(rc, 0);
    snprintf(rec, sizeof rec, "%s/k-1.ini", dir);
    char text[1024] = "";
    FILE *f = fopen(rec, "r");
    assert_non_null(f);
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    snprintf(want, sizeof want, "path = %s/clip.mp4\n", g_dir);
    assert_non_null(strstr(text, want));
}

static void save_fails_cleanly(void **state)
{
    (void)state;
    char file[128], dir[200], err[128] = "", first[64];
    write_file(file, sizeof file, "plain", "x", 1);
    snprintf(dir, sizeof dir, "%s/files", file);
    xrd_filestate st = make_state(XRD_SET_ZOOM);
    assert_int_equal(xrd_filestate_save(dir, "k-1", &st, "/v/a.mp4", err, sizeof err), -1);
    assert_true(err[0] != '\0');
    assert_int_equal(count_entries(g_dir, first, sizeof first), 1);
    assert_string_equal(first, "plain");
}

static void load_oversize(void **state)
{
    (void)state;
    char p[128], err[128] = "";
    char *big = malloc(70000);
    assert_non_null(big);
    memset(big, '#', 70000);
    big[69999] = '\n';
    write_file(p, sizeof p, "big.ini", big, 70000);
    free(big);
    xrd_filestate out;
    assert_int_equal(xrd_filestate_load(p, &out, err, sizeof err), XRD_FS_BAD);
    assert_int_equal((int)out.set, 0);
    assert_true(err[0] != '\0');
}

static void load_missing(void **state)
{
    (void)state;
    char p[160], f[128], err[128];
    xrd_filestate out;
    snprintf(p, sizeof p, "%s/nope.ini", g_dir);
    assert_int_equal(xrd_filestate_load(p, &out, err, sizeof err), XRD_FS_NONE);
    write_file(f, sizeof f, "plain", "x", 1);
    snprintf(p, sizeof p, "%s/x.ini", f);
    assert_int_equal(xrd_filestate_load(p, &out, err, sizeof err), XRD_FS_NONE);
}

static void load_directory_is_bad(void **state)
{
    (void)state;
    char err[128] = "";
    xrd_filestate out;
    assert_int_equal(xrd_filestate_load(g_dir, &out, err, sizeof err), XRD_FS_BAD);
    assert_true(err[0] != '\0');
}

/* ---- apply, collect, equal, describe ------------------------------------ */

static xrd_file_settings defaults(void)
{
    xrd_file_settings s = { 1.0f, XRD_LAYOUT_SBS_LR, false, 180.0f, 180.0f, 0.0f };
    return s;
}

static xrd_filestate zoom_fov_rec(void)
{
    xrd_filestate r;
    memset(&r, 0, sizeof r);
    r.set = XRD_SET_ZOOM | XRD_SET_CONTENT_FOV;
    r.v.zoom = 0.75f;
    r.v.content_hfov = 120.0f;
    r.v.content_vfov = 90.0f;
    return r;
}

static void apply_record_over_defaults(void **state)
{
    (void)state;
    xrd_filestate rec = zoom_fov_rec();
    xrd_file_settings s = defaults();
    assert_int_equal(xrd_filestate_apply(&rec, 0, &s), XRD_SET_ZOOM | XRD_SET_CONTENT_FOV);
    assert_float_equal(s.zoom, 0.75f, 0.0f);
    assert_float_equal(s.content_hfov, 120.0f, 0.0f);
    assert_float_equal(s.content_vfov, 90.0f, 0.0f);
    assert_int_equal(s.layout, XRD_LAYOUT_SBS_LR);
    assert_false(s.swap);
    assert_float_equal(s.convergence, 0.0f, 0.0f);
}

static void apply_flag_wins(void **state)
{
    (void)state;
    xrd_filestate rec = zoom_fov_rec();
    xrd_file_settings s = defaults();
    s.zoom = 2.0f;
    assert_int_equal(xrd_filestate_apply(&rec, XRD_SET_ZOOM, &s), XRD_SET_CONTENT_FOV);
    assert_float_equal(s.zoom, 2.0f, 0.0f);
    assert_float_equal(s.content_hfov, 120.0f, 0.0f);
}

static void apply_each_field(void **state)
{
    (void)state;
    xrd_filestate rec;
    xrd_file_settings s;

    memset(&rec, 0, sizeof rec);
    rec.v.zoom = 0.5f; rec.v.layout = XRD_LAYOUT_MONO; rec.v.swap = true;
    rec.v.content_hfov = 100.0f; rec.v.content_vfov = 60.0f; rec.v.convergence = 1.5f;

    rec.set = XRD_SET_ZOOM; s = defaults();
    assert_int_equal(xrd_filestate_apply(&rec, 0, &s), XRD_SET_ZOOM);
    assert_float_equal(s.zoom, 0.5f, 0.0f);
    assert_int_equal(s.layout, XRD_LAYOUT_SBS_LR);
    assert_false(s.swap);
    assert_float_equal(s.content_hfov, 180.0f, 0.0f);
    assert_float_equal(s.convergence, 0.0f, 0.0f);

    rec.set = XRD_SET_LAYOUT; s = defaults();
    assert_int_equal(xrd_filestate_apply(&rec, 0, &s), XRD_SET_LAYOUT);
    assert_int_equal(s.layout, XRD_LAYOUT_MONO);
    assert_float_equal(s.zoom, 1.0f, 0.0f);
    assert_false(s.swap);

    rec.set = XRD_SET_SWAP; s = defaults();
    assert_int_equal(xrd_filestate_apply(&rec, 0, &s), XRD_SET_SWAP);
    assert_true(s.swap);
    assert_int_equal(s.layout, XRD_LAYOUT_SBS_LR);

    rec.set = XRD_SET_CONTENT_FOV; s = defaults();
    assert_int_equal(xrd_filestate_apply(&rec, 0, &s), XRD_SET_CONTENT_FOV);
    assert_float_equal(s.content_hfov, 100.0f, 0.0f);
    assert_float_equal(s.content_vfov, 60.0f, 0.0f);
    assert_float_equal(s.zoom, 1.0f, 0.0f);

    rec.set = XRD_SET_CONVERGENCE; s = defaults();
    assert_int_equal(xrd_filestate_apply(&rec, 0, &s), XRD_SET_CONVERGENCE);
    assert_float_equal(s.convergence, 1.5f, 0.0f);
    assert_float_equal(s.content_hfov, 180.0f, 0.0f);
}

static void collect_rule1_loaded(void **state)
{
    (void)state;
    xrd_filestate loaded;
    memset(&loaded, 0, sizeof loaded);
    loaded.set = XRD_SET_ZOOM;
    loaded.v.zoom = 0.75f;
    xrd_file_settings start = defaults(), end;
    start.zoom = 0.75f;
    end = start;
    xrd_filestate out;
    xrd_filestate_collect(&loaded, 0, &start, &end, &out);
    assert_int_equal(out.set, XRD_SET_ZOOM);
    assert_float_equal(out.v.zoom, 0.75f, 0.0f);
}

static void collect_rule2_flag(void **state)
{
    (void)state;
    xrd_file_settings start = defaults(), end = defaults();
    start.content_hfov = start.content_vfov = 90.0f;
    end = start;
    xrd_filestate out;
    xrd_filestate_collect(NULL, XRD_SET_CONTENT_FOV, &start, &end, &out);
    assert_int_equal(out.set, XRD_SET_CONTENT_FOV);
    assert_float_equal(out.v.content_hfov, 90.0f, 0.0f);
    assert_float_equal(out.v.content_vfov, 90.0f, 0.0f);
}

static void collect_rule3_changed(void **state)
{
    (void)state;
    xrd_file_settings start = defaults(), end = start;
    xrd_filestate out;

    end.zoom = start.zoom / 1.1f;
    xrd_filestate_collect(NULL, 0, &start, &end, &out);
    assert_int_equal(out.set, XRD_SET_ZOOM);

    end = start;
    end.layout = XRD_LAYOUT_SBS_RL;
    xrd_filestate_collect(NULL, 0, &start, &end, &out);
    assert_int_equal(out.set, XRD_SET_LAYOUT);
    assert_int_equal(out.v.layout, XRD_LAYOUT_SBS_RL);

    end = start;
    end.swap = !start.swap;
    xrd_filestate_collect(NULL, 0, &start, &end, &out);
    assert_int_equal(out.set, XRD_SET_SWAP);
    assert_true(out.v.swap);
}

static void collect_zoom_out_and_back(void **state)
{
    (void)state;
    xrd_file_settings start = defaults(), end = start;
    float z = 1.0f;
    z /= 1.1f;
    z *= 1.1f;
    end.zoom = z;
    xrd_filestate out;
    xrd_filestate_collect(NULL, 0, &start, &end, &out);
    assert_int_equal(out.set, 0);
}

static void collect_nothing(void **state)
{
    (void)state;
    xrd_file_settings start = defaults(), end = start;
    xrd_filestate out;
    xrd_filestate_collect(NULL, 0, &start, &end, &out);
    assert_int_equal(out.set, 0);
}

static void collect_values_from_end(void **state)
{
    (void)state;
    xrd_filestate loaded;
    memset(&loaded, 0, sizeof loaded);
    loaded.set = XRD_SET_ZOOM;
    loaded.v.zoom = 0.75f;
    xrd_file_settings start = defaults(), end = defaults();
    start.zoom = 0.75f;
    end.zoom = 0.5f;
    xrd_filestate out;
    xrd_filestate_collect(&loaded, 0, &start, &end, &out);
    assert_int_equal(out.set, XRD_SET_ZOOM);
    assert_float_equal(out.v.zoom, 0.5f, 0.0f);
}

static void equal_tolerances(void **state)
{
    (void)state;
    xrd_filestate a = zoom_fov_rec(), b = zoom_fov_rec();
    assert_true(xrd_filestate_equal(&a, &b));
    b.v.zoom = a.v.zoom + 0.0005f;
    assert_true(xrd_filestate_equal(&a, &b));
    b.v.zoom = a.v.zoom + 0.01f;
    assert_false(xrd_filestate_equal(&a, &b));

    b = a;
    b.set |= XRD_SET_SWAP;
    assert_false(xrd_filestate_equal(&a, &b));

    b = a;
    b.v.layout = XRD_LAYOUT_MONO;   /* not set, so ignored */
    b.v.swap = true;
    b.v.convergence = 3.0f;
    assert_true(xrd_filestate_equal(&a, &b));
}

static void describe_formats(void **state)
{
    (void)state;
    xrd_filestate st;
    memset(&st, 0, sizeof st);
    st.set = XRD_SET_ZOOM | XRD_SET_LAYOUT | XRD_SET_SWAP | XRD_SET_CONTENT_FOV
           | XRD_SET_CONVERGENCE;
    st.v.zoom = 0.75f; st.v.layout = XRD_LAYOUT_SBS_RL; st.v.swap = true;
    st.v.content_hfov = 120.0f; st.v.content_vfov = 90.0f; st.v.convergence = 1.5f;

    char buf[128];
    int n = xrd_filestate_describe(&st, st.set, buf, sizeof buf);
    assert_string_equal(buf, "zoom=0.75 layout=sbs-rl swap=yes content-fov=120x90 convergence=1.5");
    assert_int_equal(n, (int)strlen(buf));

    n = xrd_filestate_describe(&st, XRD_SET_CONTENT_FOV, buf, sizeof buf);
    assert_string_equal(buf, "content-fov=120x90");
    assert_int_equal(n, (int)strlen(buf));

    st.v.swap = false;
    xrd_filestate_describe(&st, XRD_SET_SWAP, buf, sizeof buf);
    assert_string_equal(buf, "swap=no");

    strcpy(buf, "junk");
    assert_int_equal(xrd_filestate_describe(&st, 0, buf, sizeof buf), 0);
    assert_string_equal(buf, "");
}

static void describe_too_small(void **state)
{
    (void)state;
    xrd_filestate st;
    memset(&st, 0, sizeof st);
    st.set = XRD_SET_ZOOM;
    st.v.zoom = 0.75f;
    char buf[8];
    assert_int_equal(xrd_filestate_describe(&st, st.set, buf, sizeof buf), -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(fnv_vectors),
        cmocka_unit_test_setup_teardown(key_small_files, setup, teardown),
        cmocka_unit_test_setup_teardown(key_overlap_and_large, setup, teardown),
        cmocka_unit_test_setup_teardown(key_survives_rename, setup, teardown),
        cmocka_unit_test_setup_teardown(key_changes_on_edit, setup, teardown),
        cmocka_unit_test_setup_teardown(key_rejects_non_files, setup, teardown),
        cmocka_unit_test_setup_teardown(key_follows_symlink, setup, teardown),
        cmocka_unit_test_setup_teardown(key_buffer_too_small, setup, teardown),
        cmocka_unit_test_setup_teardown(dir_selection, setup, teardown),
        cmocka_unit_test_setup_teardown(format_only_set_fields, setup, teardown),
        cmocka_unit_test_setup_teardown(format_all_fields, setup, teardown),
        cmocka_unit_test_setup_teardown(format_too_small, setup, teardown),
        cmocka_unit_test_setup_teardown(round_trip, setup, teardown),
        cmocka_unit_test_setup_teardown(parse_minimal, setup, teardown),
        cmocka_unit_test_setup_teardown(parse_tolerates, setup, teardown),
        cmocka_unit_test_setup_teardown(parse_duplicate_last_wins, setup, teardown),
        cmocka_unit_test_setup_teardown(parse_bad, setup, teardown),
        cmocka_unit_test_setup_teardown(parse_bad_reports_line, setup, teardown),
        cmocka_unit_test_setup_teardown(parse_unsupported, setup, teardown),
        cmocka_unit_test_setup_teardown(path_label_cannot_inject, setup, teardown),
        cmocka_unit_test_setup_teardown(record_path, setup, teardown),
        cmocka_unit_test_setup_teardown(save_load_delete, setup, teardown),
        cmocka_unit_test_setup_teardown(save_relative_path_label, setup, teardown),
        cmocka_unit_test_setup_teardown(save_fails_cleanly, setup, teardown),
        cmocka_unit_test_setup_teardown(load_oversize, setup, teardown),
        cmocka_unit_test_setup_teardown(load_missing, setup, teardown),
        cmocka_unit_test_setup_teardown(load_directory_is_bad, setup, teardown),
        cmocka_unit_test(apply_record_over_defaults),
        cmocka_unit_test(apply_flag_wins),
        cmocka_unit_test(apply_each_field),
        cmocka_unit_test(collect_rule1_loaded),
        cmocka_unit_test(collect_rule2_flag),
        cmocka_unit_test(collect_rule3_changed),
        cmocka_unit_test(collect_zoom_out_and_back),
        cmocka_unit_test(collect_nothing),
        cmocka_unit_test(collect_values_from_end),
        cmocka_unit_test(equal_tolerances),
        cmocka_unit_test(describe_formats),
        cmocka_unit_test(describe_too_small),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
