#ifndef XRD_DEVICES_H
#define XRD_DEVICES_H

#include <stdbool.h>

/* The glasses xrdome knows without a [device.<name>] section in the config,
 * and their diagonal field of view in degrees. One table for every front end
 * (Linux and Android), so a device has the same FOV everywhere. */

/* The built-in names, for messages; keep in step with the table in devices.c. */
#define XRD_DEVICE_BUILTIN_NAMES "viture-pro, xreal-air2pro"

/* True when name is a built-in device; *fov_diag is then set to its diagonal
 * FOV. False, *fov_diag untouched, otherwise. */
bool xrd_device_builtin(const char *name, float *fov_diag);

#endif
