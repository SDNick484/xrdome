#ifndef XRD_PLATFORM_H
#define XRD_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

/* The operating system xrdome was built for, and what follows from it.
 * The platform is fixed at compile time (xrd_platform_current); every other
 * function takes it as an argument so every platform is unit tested on
 * any host. Display-server differences inside Linux (Xorg vs XWayland) stay in
 * display/output.c. */
typedef enum {
    XRD_PLATFORM_LINUX,
    XRD_PLATFORM_MACOS,
    XRD_PLATFORM_WINDOWS,
    XRD_PLATFORM_OTHER,
} xrd_platform;

xrd_platform xrd_platform_current(void);
const char  *xrd_platform_name(xrd_platform p); /* "linux", "macos", "windows", "other" */

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

/* ---- per-user paths --------------------------------------------------- */

typedef enum {
    XRD_USER_CONFIG, /* the config file */
    XRD_USER_STATE,  /* the directory of per-file records */
} xrd_user_path_kind;

/* path starts at a root: '/' anywhere; on Windows also a backslash, and a
 * drive letter followed by ':' and either slash. */
bool xrd_platform_path_absolute(xrd_platform p, const char *path);

/* Where xrdome keeps its config file or records, from the values passed in
 * (a pure function, so every platform is tested on any host):
 *   xdg    XDG_CONFIG_HOME (CONFIG) or XDG_STATE_HOME (STATE); when it is
 *          absolute it wins on every platform (the tests set it):
 *          <xdg>/xrdome/config.ini, <xdg>/xrdome/files
 *   home   HOME; Linux, macOS, other: <home>/.config/xrdome/config.ini,
 *          <home>/.local/state/xrdome/files
 *   known  Windows: the known folder, %APPDATA% (CONFIG) or %LOCALAPPDATA%
 *          (STATE): <known>\xrdome\config.ini, <known>\xrdome\files.
 *          HOME is not used there.
 * Any of them may be NULL. 0, or -1 when none applies or it does not fit. */
int xrd_platform_user_path(xrd_platform p, xrd_user_path_kind kind, const char *xdg,
                           const char *home, const char *known, char *buf, size_t len);

/* xrd_platform_user_path for this build, from the environment and (Windows)
 * SHGetKnownFolderPath. */
int xrd_user_path(xrd_user_path_kind kind, char *buf, size_t len);

/* ---- files -------------------------------------------------------------- */

/* Creates one directory, private to the user where the platform has modes
 * (0700); Windows' mkdir takes no mode. 0, or -1 with errno set. */
int xrd_mkdir(const char *path);

/* Renames from over to, replacing an existing to atomically: rename() on
 * POSIX; on Windows, where rename() fails over an existing file,
 * MoveFileExW(MOVEFILE_REPLACE_EXISTING), the names taken as UTF-8.
 * 0, or -1 with errno set. */
int xrd_replace_file(const char *from, const char *to);

#endif
