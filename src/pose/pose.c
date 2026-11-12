#include "pose/pose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pose/pose_internal.h"

static void set_err(char *err, size_t errlen, const char *msg)
{
    if (err && errlen)
        snprintf(err, errlen, "%s", msg);
}

xrd_pose_source *xrd_pose_source_create(const char *spec, char *err, size_t errlen)
{
    set_err(err, errlen, "");
    if (!spec || !*spec) {
        set_err(err, errlen, "empty pose backend spec");
        return NULL;
    }

    const char *colon = strchr(spec, ':');
    size_t nlen = colon ? (size_t)(colon - spec) : strlen(spec);
    const char *args = colon ? colon + 1 : "";

    xrd_pose_source *s = NULL;
    if (nlen == 4 && strncmp(spec, "null", 4) == 0)
        s = xrd_pose_null_alloc();
    else if (nlen == 13 && strncmp(spec, "udp-opentrack", 13) == 0)
        s = xrd_pose_udp_alloc();
    else {
        char msg[160];
        snprintf(msg, sizeof msg, "unknown pose backend '%.*s'", (int)nlen, spec);
        set_err(err, errlen, msg);
        return NULL;
    }
    if (!s) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }

    if (s->open(s, args) != 0) {
        if (strcmp(s->name, "udp-opentrack") == 0)
            set_err(err, errlen, xrd_pose_udp_open_error(s));
        else
            set_err(err, errlen, "failed to open pose backend");
        free(s);
        return NULL;
    }
    return s;
}

void xrd_pose_source_destroy(xrd_pose_source *s)
{
    if (!s)
        return;
    s->close(s);
    free(s);
}
