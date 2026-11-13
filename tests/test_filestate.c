#define _POSIX_C_SOURCE 200809L /* mkdtemp, setenv, symlink */

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
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
