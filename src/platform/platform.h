#ifndef XRD_PLATFORM_H
#define XRD_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

/* xrd_platform, the per-user paths and the file helpers (the core's part). */
#include "platform/paths.h"

/* What the front end (SDL, mpv, the window) needs to know about the
 * platform; see paths.h for the platform itself. */

/* mpv hwdec used when neither --hwdec nor [general] hwdec is set:
 * linux "vaapi" (unchanged from before the option existed), macos
 * "videotoolbox", windows "d3d11va-copy" (libmpv's GL render API maps D3D11
 * frames into GL only through ANGLE; xrdome uses native WGL, so the decoded
 * frame is copied), other "auto-safe". */
const char *xrd_platform_default_hwdec(xrd_platform p);

/* SDL must use its X11 driver with EGL, and mpv gets the X11 Display
 * (Linux: Xorg or XWayland). False on macOS (Cocoa, CGL) and Windows (SDL's
 * windows driver, WGL). */
bool xrd_platform_uses_x11(xrd_platform p);

/* The GL 3.3 core context must be requested forward-compatible (macOS
 * refuses a core context without it). Linux and Windows keep the plain core
 * request. */
bool xrd_platform_gl_forward_compat(xrd_platform p);

/* The window goes fullscreen (its own Space on macOS; desktop fullscreen on
 * Windows) on the glasses rather than borderless at the output's position,
 * which macOS would put under the menu bar and Windows may leave behind the
 * taskbar. Linux decides by display server (xrd_output_use_fullscreen). */
bool xrd_platform_force_fullscreen(xrd_platform p);

/* XRLinuxDriver (and so the automatic SBS switch through its state files)
 * can exist on this platform. */
bool xrd_platform_has_xrdriver(xrd_platform p);

/* True when mpv's hwdec-current names a decoder whose frames reach GL
 * without a copy through system memory: not empty, not "no", and not a
 * "-copy" variant (vaapi, nvdec, cuda, videotoolbox, vdpau, drm, ...). */
bool xrd_hwdec_is_zero_copy(const char *current);

/* The note for --require-hwdec's refusal and the software-decoding warning
 * when the default hwdec is itself a copy (Windows): "" elsewhere. */
const char *xrd_platform_hwdec_note(xrd_platform p);

/* A --hwdec / [general] hwdec value: 1..31 characters of [a-z0-9-,]
 * (mpv accepts a comma-separated priority list). */
bool xrd_hwdec_name_valid(const char *name);

#endif
