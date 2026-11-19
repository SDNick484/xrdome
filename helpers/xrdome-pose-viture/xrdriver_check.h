#ifndef XRD_VITURE_XRDRIVER_CHECK_H
#define XRD_VITURE_XRDRIVER_CHECK_H

#include <stdbool.h>

/* XRLinuxDriver and VITURE's SDK both open the glasses' USB interfaces, so
 * only one can run. The driver rewrites <dir>/xr_driver_state every second,
 * with a line heartbeat=<epoch seconds>. <dir> is /dev/shm, or
 * $XRDOME_XR_DRIVER_DIR when set and not empty (the tests' hook, as in
 * xrdome). */

/* The directory the state file is in. */
const char *viture_xrdriver_dir(void);

/* True when the state file in dir has a heartbeat within 5 seconds of now
 * (epoch seconds), either way: the driver is running. A missing or unreadable
 * file, or no heartbeat line, is false. */
bool viture_xrdriver_running(const char *dir, long long now);

#endif
