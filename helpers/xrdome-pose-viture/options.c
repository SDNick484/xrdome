#include "options.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char USAGE[] =
    "usage: xrdome-pose-viture [options]\n"
    "\n"
    "Reads the head pose of VITURE glasses through VITURE's SDK library and sends it\n"
    "as OpenTrack UDP, which `xrdome --pose udp-opentrack` reads.\n"
    "\n"
    "  --port N     UDP port to send to (default 4242)\n"
    "  --host ADDR  IPv4 address to send to (default 127.0.0.1)\n"
    "  --rate HZ    send at most HZ packets per second, 1 to 1000 (default 250)\n"
    "  --sbs        switch the glasses to 3840x1080 side by side while running, and\n"
    "               back when the helper exits (also on Ctrl+C)\n"
    "  --lib PATH   VITURE's SDK library (default: libglasses next to the system's\n"
    "               library path; the SDK's own folder holds it)\n"
    "  --pid N      USB product id of the glasses (0x1019 or 0x101D for the Pro),\n"
    "               when they are not found by themselves\n"
    "  --count N    stop after N packets (a check; default: run until Ctrl+C)\n"
    "  --version    print the version\n"
    "  --help       print this text\n";

const char *viture_usage(void)
{
    return USAGE;
}

static int fail(char *err, size_t errlen, const char *fmt, const char *a)
{
    snprintf(err, errlen, fmt, a);
    return -1;
}

/* A whole-string integer in [lo, hi]; base 0 accepts 0x1019. */
static int parse_long(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end != '\0' || v < lo || v > hi)
        return -1;
    *out = v;
    return 0;
}

int viture_parse_args(int argc, char **argv, viture_options *o, char *err, size_t errlen)
{
    memset(o, 0, sizeof *o);
    o->host = VITURE_DEFAULT_HOST;
    o->port = VITURE_DEFAULT_PORT;
    o->rate = VITURE_DEFAULT_RATE;
    err[0] = '\0';

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (strcmp(arg, "-h") == 0) {
            o->help = true;
            continue;
        }
        if (strncmp(arg, "--", 2) != 0)
            return fail(err, errlen, "unexpected argument '%.60s' (options start with --)", arg);

        /* "--name=value": split in place of a copy of the name. */
        char name[16];
        const char *val = NULL;
        const char *eq = strchr(arg, '=');
        size_t nlen = eq ? (size_t)(eq - arg) : strlen(arg);
        if (nlen >= sizeof name)
            return fail(err, errlen, "unknown option '%.60s'", arg);
        memcpy(name, arg, nlen);
        name[nlen] = '\0';
        if (eq)
            val = eq + 1;

        if (strcmp(name, "--help") == 0) {
            o->help = true;
            continue;
        }
        if (strcmp(name, "--version") == 0) {
            o->version = true;
            continue;
        }
        if (strcmp(name, "--sbs") == 0) {
            if (val)
                return fail(err, errlen, "%.20s takes no value", name);
            o->sbs = true;
            continue;
        }

        int is_port = strcmp(name, "--port") == 0, is_host = strcmp(name, "--host") == 0,
            is_rate = strcmp(name, "--rate") == 0, is_lib = strcmp(name, "--lib") == 0,
            is_pid = strcmp(name, "--pid") == 0, is_count = strcmp(name, "--count") == 0;
        if (!is_port && !is_host && !is_rate && !is_lib && !is_pid && !is_count)
            return fail(err, errlen, "unknown option '%.60s'", arg);
        if (!val) {
            if (i + 1 >= argc)
                return fail(err, errlen, "%.20s needs a value", name);
            val = argv[++i];
        }

        long n;
        if (is_port) {
            if (parse_long(val, 1, 65535, &n) != 0)
                return fail(err, errlen, "--port must be 1 to 65535, got '%.40s'", val);
            o->port = (int)n;
        } else if (is_host) {
            if (val[0] == '\0')
                return fail(err, errlen, "--host needs an address%s", "");
            o->host = val;
        } else if (is_rate) {
            char *end;
            errno = 0;
            double r = strtod(val, &end);
            if (errno || end == val || *end != '\0' || !isfinite(r) || r < 1.0 || r > VITURE_MAX_RATE)
                return fail(err, errlen, "--rate must be 1 to 1000, got '%.40s'", val);
            o->rate = r;
        } else if (is_lib) {
            if (val[0] == '\0')
                return fail(err, errlen, "--lib needs a path%s", "");
            o->lib = val;
        } else if (is_pid) {
            if (parse_long(val, 1, 0xFFFF, &n) != 0)
                return fail(err, errlen, "--pid must be a USB product id (1 to 0xFFFF), got '%.40s'", val);
            o->pid = (int)n;
        } else {
            if (parse_long(val, 1, 2000000000L, &n) != 0)
                return fail(err, errlen, "--count must be a positive number, got '%.40s'", val);
            o->count = n;
        }
    }
    return 0;
}
