#ifndef XRD_PLATFORM_PATHS_H
#define XRD_PLATFORM_PATHS_H

#include <stdbool.h>
#include <stddef.h>

/* The part of platform.h the core needs (libxrdome-core, src/core_sources.txt):
 * which platform this is, absolute paths, the per-user paths and the file
 * helpers. No window system, GL or video player knowledge here. */

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
