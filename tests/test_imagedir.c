#define _XOPEN_SOURCE 700 /* mkdtemp, symlink */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmocka.h>

#include "app/imagedir.h"

static char g_dir[64];

static void touch(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    FILE *f = fopen(path, "w");
    assert_non_null(f);
    fputs("x", f);
    fclose(f);
}

static int setup(void **state)
{
    (void)state;
    strcpy(g_dir, "/tmp/xrd_imgdir_XXXXXX");
    assert_non_null(mkdtemp(g_dir));
    return 0;
}

static int teardown(void **state)
{
    (void)state;
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
    if (system(cmd) != 0) { /* best effort */ }
    return 0;
}

static const char *base(const xrd_imagedir *d, size_t i)
{
    return strrchr(d->path[i], '/') + 1;
}

static void lists_images_in_name_order(void **state)
{
    (void)state;
    touch("b.jpg");
    touch("a.PNG");
    touch("IMG_10.jpeg");
    touch("IMG_2.jpeg");
    touch("notes.txt");
    touch("film.mp4");
    touch(".hidden.jpg");
    touch("c.webp");
    char sub[256];
    snprintf(sub, sizeof sub, "%s/sub.jpg", g_dir); /* a directory named like an image */
    assert_int_equal(mkdir(sub, 0700), 0);

    char err[200] = "";
    xrd_imagedir d;
    assert_int_equal(xrd_imagedir_open(g_dir, &d, err, sizeof err), 0);
    assert_int_equal((int)d.count, 5);
    const char *want[] = {"IMG_10.jpeg", "IMG_2.jpeg", "a.PNG", "b.jpg", "c.webp"};
    for (size_t i = 0; i < 5; i++)
        assert_string_equal(base(&d, i), want[i]);
    char first[300];
    snprintf(first, sizeof first, "%s/IMG_10.jpeg", g_dir);
    assert_string_equal(d.path[0], first);
    xrd_imagedir_free(&d);
    assert_null(d.path);
    assert_int_equal((int)d.count, 0);
}

static void trailing_slash_and_symlink(void **state)
{
    (void)state;
    char link[256], target[256], dirs[256], err[200] = "";
    snprintf(target, sizeof target, "%s/real.png", g_dir);
    snprintf(link, sizeof link, "%s/link.jpg", g_dir);
    assert_int_equal(symlink(target, link), 0);
    xrd_imagedir d;
    /* A dangling link is not listed. */
    assert_int_equal(xrd_imagedir_open(g_dir, &d, err, sizeof err), -1);
    touch("real.png"); /* now both resolve */
    snprintf(dirs, sizeof dirs, "%s/", g_dir);
    assert_int_equal(xrd_imagedir_open(dirs, &d, err, sizeof err), 0);
    assert_int_equal((int)d.count, 2);
    assert_string_equal(base(&d, 0), "link.jpg");
    assert_string_equal(base(&d, 1), "real.png");
    assert_null(strstr(d.path[0] + strlen(g_dir) + 1, "/")); /* one slash, not two */
    assert_null(strstr(d.path[0], "//"));
    xrd_imagedir_free(&d);
}

static void errors(void **state)
{
    (void)state;
    char err[200] = "";
    xrd_imagedir d;
    touch("notes.txt");
    assert_int_equal(xrd_imagedir_open(g_dir, &d, err, sizeof err), -1);
    assert_non_null(strstr(err, "no images"));
    assert_non_null(strstr(err, g_dir));
    assert_null(d.path);
    err[0] = '\0';
    assert_int_equal(xrd_imagedir_open("/tmp/xrd_imgdir_missing/none", &d, err, sizeof err), -1);
    assert_non_null(strstr(err, "cannot read directory"));
}

static void is_dir(void **state)
{
    (void)state;
    assert_true(xrd_imagedir_is_dir(g_dir));
    touch("a.jpg");
    char f[256];
    snprintf(f, sizeof f, "%s/a.jpg", g_dir);
    assert_false(xrd_imagedir_is_dir(f));
    assert_false(xrd_imagedir_is_dir("/tmp/xrd_imgdir_missing"));
    assert_false(xrd_imagedir_is_dir(NULL));
}

static void step_does_not_wrap(void **state)
{
    (void)state;
    touch("a.jpg");
    touch("b.jpg");
    touch("c.jpg");
    char err[200];
    xrd_imagedir d;
    assert_int_equal(xrd_imagedir_open(g_dir, &d, err, sizeof err), 0);
    assert_int_equal(xrd_imagedir_step(&d, 0, 1), 1);
    assert_int_equal(xrd_imagedir_step(&d, 1, 1), 2);
    assert_int_equal(xrd_imagedir_step(&d, 2, 1), -1);
    assert_int_equal(xrd_imagedir_step(&d, 2, -1), 1);
    assert_int_equal(xrd_imagedir_step(&d, 0, -1), -1);
    xrd_imagedir_free(&d);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(lists_images_in_name_order, setup, teardown),
        cmocka_unit_test_setup_teardown(trailing_slash_and_symlink, setup, teardown),
        cmocka_unit_test_setup_teardown(errors, setup, teardown),
        cmocka_unit_test_setup_teardown(is_dir, setup, teardown),
        cmocka_unit_test_setup_teardown(step_does_not_wrap, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
