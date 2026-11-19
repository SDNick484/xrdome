#include "xrdriver_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRESH_S 5

const char *viture_xrdriver_dir(void)
{
    const char *d = getenv("XRDOME_XR_DRIVER_DIR");
    return d && d[0] ? d : "/dev/shm";
}

bool viture_xrdriver_running(const char *dir, long long now)
{
    char path[512];
    int n = snprintf(path, sizeof path, "%s/xr_driver_state", dir);
    if (n < 0 || (size_t)n >= sizeof path)
        return false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool fresh = false;
    char line[256];
    /* Lines longer than the buffer (the license JSON) are read in pieces and
     * hold no heartbeat: only a piece that starts a line is looked at. */
    bool start = true;
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        bool whole = len > 0 && line[len - 1] == '\n';
        if (start && strncmp(line, "heartbeat=", 10) == 0) {
            char *end;
            long long hb = strtoll(line + 10, &end, 10);
            if (end != line + 10 && (*end == '\n' || *end == '\0'))
                fresh = hb - now <= FRESH_S && now - hb <= FRESH_S;
            break;
        }
        start = whole;
    }
    fclose(f);
    return fresh;
}
