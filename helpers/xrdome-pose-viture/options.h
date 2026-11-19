#ifndef XRD_VITURE_OPTIONS_H
#define XRD_VITURE_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#define VITURE_DEFAULT_HOST "127.0.0.1"
#define VITURE_DEFAULT_PORT 4242
#define VITURE_DEFAULT_RATE 250.0
#define VITURE_MAX_RATE 1000.0

typedef struct {
    const char *host;  /* IPv4 address packets go to */
    int port;
    double rate;       /* cap on packets per second */
    bool sbs;          /* switch the glasses to 3840x1080 while running */
    const char *lib;   /* SDK library path or name; NULL = the platform's default */
    int pid;           /* USB product id; 0 = find the glasses */
    long count;        /* stop after this many packets; 0 = run until stopped */
    bool help;
    bool version;
} viture_options;

/* argv[0] is skipped. Values come as "--port N" or "--port=N"; the strings in
 * *o point into argv. Returns 0, or -1 with a one-line message in err. */
int viture_parse_args(int argc, char **argv, viture_options *o, char *err, size_t errlen);

/* The text --help prints. */
const char *viture_usage(void);

#endif
