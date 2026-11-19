#ifndef XRD_PLATFORM_H
#define XRD_PLATFORM_H

#include <stdbool.h>

/* The operating system xrdome was built for, and what follows from it.
 * The platform is fixed at compile time (xrd_platform_current); every other
 * function takes it as an argument so both platforms are unit tested on
 * either. Display-server differences inside Linux (Xorg vs XWayland) stay in
 * display/output.c. */
typedef enum {
    XRD_PLATFORM_LINUX,
    XRD_PLATFORM_MACOS,
    XRD_PLATFORM_OTHER,
} xrd_platform;

xrd_platform xrd_platform_current(void);
const char  *xrd_platform_name(xrd_platform p); /* "linux", "macos", "other" */

/* mpv hwdec used when neither --hwdec nor [general] hwdec is set:
 * linux "vaapi" (unchanged from before the option existed), macos
 * "videotoolbox", other "auto-safe". */
const char *xrd_platform_default_hwdec(xrd_platform p);

/* SDL must use its X11 driver with EGL, and mpv gets the X11 Display
 * (Linux: Xorg or XWayland). False on macOS (Cocoa, CGL). */
bool xrd_platform_uses_x11(xrd_platform p);

/* The GL 3.3 core context must be requested forward-compatible (macOS
 * refuses a core context without it). Linux keeps its request unchanged. */
bool xrd_platform_gl_forward_compat(xrd_platform p);

/* The window goes fullscreen (its own Space on macOS) on the glasses rather
 * than borderless at the output's position, which macOS would put under the
 * menu bar. Linux decides by display server (xrd_output_use_fullscreen). */
bool xrd_platform_force_fullscreen(xrd_platform p);

/* XRLinuxDriver (and so the automatic SBS switch through its state files)
 * can exist on this platform. */
bool xrd_platform_has_xrdriver(xrd_platform p);

/* True when mpv's hwdec-current names a decoder whose frames reach GL
 * without a copy through system memory: not empty, not "no", and not a
 * "-copy" variant (vaapi, nvdec, cuda, videotoolbox, vdpau, drm, ...). */
bool xrd_hwdec_is_zero_copy(const char *current);

/* A --hwdec / [general] hwdec value: 1..31 characters of [a-z0-9-,]
 * (mpv accepts a comma-separated priority list). */
bool xrd_hwdec_name_valid(const char *name);

#endif
