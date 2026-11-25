#include "devices.h"

#include <stddef.h>
#include <string.h>

static const struct { const char *name; float fov_diag; } builtin_devices[] = {
    {"viture-pro", 43.0f},
    {"xreal-air2pro", 45.0f},
};

bool xrd_device_builtin(const char *name, float *fov_diag)
{
    if (!name)
        return false;
    for (size_t i = 0; i < sizeof builtin_devices / sizeof builtin_devices[0]; i++) {
        if (!strcmp(name, builtin_devices[i].name)) {
            *fov_diag = builtin_devices[i].fov_diag;
            return true;
        }
    }
    return false;
}
