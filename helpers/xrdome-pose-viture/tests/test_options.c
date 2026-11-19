#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "options.h"

static int parse(viture_options *o, char *err, int argc, ...)
{
    char *argv[16];
    va_list ap;
    va_start(ap, argc);
    argv[0] = (char *)"xrdome-pose-viture";
    for (int i = 1; i < argc; i++)
        argv[i] = va_arg(ap, char *);
    va_end(ap);
    return viture_parse_args(argc, argv, o, err, 200);
}

static void defaults(void **state)
{
    (void)state;
    viture_options o;
    char err[200];
    assert_int_equal(parse(&o, err, 1), 0);
    assert_string_equal(o.host, "127.0.0.1");
    assert_int_equal(o.port, 4242);
    assert_true(o.rate == VITURE_DEFAULT_RATE);
    assert_false(o.sbs);
    assert_null(o.lib);
    assert_int_equal(o.pid, 0);
    assert_int_equal(o.count, 0);
    assert_false(o.help || o.version);
}

static void every_option(void **state)
{
    (void)state;
    viture_options o;
    char err[200];
    assert_int_equal(parse(&o, err, 15, "--port", "5000", "--host", "10.0.0.2", "--rate", "90.5", "--sbs",
                           "--lib", "/sdk/libglasses.so", "--pid", "0x101D", "--count", "12", "--help"),
                     0);
    assert_int_equal(o.port, 5000);
    assert_string_equal(o.host, "10.0.0.2");
    assert_true(o.rate == 90.5);
    assert_true(o.sbs);
    assert_string_equal(o.lib, "/sdk/libglasses.so");
    assert_int_equal(o.pid, 0x101D);
    assert_int_equal(o.count, 12);
    assert_true(o.help);
}

static void equals_form(void **state)
{
    (void)state;
    viture_options o;
    char err[200];
    assert_int_equal(parse(&o, err, 4, "--port=4343", "--lib=C:\\sdk\\libglasses.dll", "--pid=4121"), 0);
    assert_int_equal(o.port, 4343);
    assert_string_equal(o.lib, "C:\\sdk\\libglasses.dll");
    assert_int_equal(o.pid, 4121);
}

static void help_and_version(void **state)
{
    (void)state;
    viture_options o;
    char err[200];
    assert_int_equal(parse(&o, err, 2, "-h"), 0);
    assert_true(o.help);
    assert_int_equal(parse(&o, err, 2, "--version"), 0);
    assert_true(o.version);
    assert_non_null(strstr(viture_usage(), "--sbs"));
}

static void expect_error(const char *needle, int argc, ...)
{
    char *argv[8];
    va_list ap;
    va_start(ap, argc);
    argv[0] = (char *)"x";
    for (int i = 1; i < argc; i++)
        argv[i] = va_arg(ap, char *);
    va_end(ap);
    viture_options o;
    char err[200] = "";
    assert_int_equal(viture_parse_args(argc, argv, &o, err, sizeof err), -1);
    if (!strstr(err, needle))
        fail_msg("error '%s' lacks '%s'", err, needle);
}

static void bad_values(void **state)
{
    (void)state;
    expect_error("--port", 3, "--port", "0");
    expect_error("--port", 3, "--port", "65536");
    expect_error("--port", 3, "--port", "42x");
    expect_error("--port", 3, "--port", "");
    expect_error("--rate", 3, "--rate", "0");
    expect_error("--rate", 3, "--rate", "1001");
    expect_error("--rate", 3, "--rate", "fast");
    expect_error("--rate", 3, "--rate", "nan");
    expect_error("--pid", 3, "--pid", "0");
    expect_error("--pid", 3, "--pid", "0x10000");
    expect_error("--count", 3, "--count", "-1");
    expect_error("--count", 3, "--count", "0");
    expect_error("--lib", 3, "--lib", "");
    expect_error("--host", 3, "--host", "");
}

static void bad_shape(void **state)
{
    (void)state;
    expect_error("unknown option", 2, "--bogus");
    expect_error("unknown option", 2, "--bogus=1");
    expect_error("unknown option", 2, "--aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    expect_error("needs a value", 2, "--port");
    expect_error("needs a value", 2, "--lib");
    expect_error("takes no value", 2, "--sbs=1");
    expect_error("unexpected argument", 2, "video.mp4");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(defaults),
        cmocka_unit_test(every_option),
        cmocka_unit_test(equals_form),
        cmocka_unit_test(help_and_version),
        cmocka_unit_test(bad_values),
        cmocka_unit_test(bad_shape),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
