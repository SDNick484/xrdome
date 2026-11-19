#ifndef XRD_EXEPATH_H
#define XRD_EXEPATH_H

#include <stdbool.h>
#include <stddef.h>

#include "platform/platform.h"

/* Where the ffprobe program comes from when [general] ffprobe is not set.
 * A bundle (the macOS and Windows zips from CI) ships ffprobe beside xrdome;
 * a normal install has it on PATH. The choice is a pure function of its
 * inputs and of the platform's path rules (Windows: backslashes, ';' between
 * PATH entries, case-insensitive names, ffprobe.exe), so every platform is
 * tested on any host; the two small functions below supply the executable's
 * own directory. */

/* The directory of the running executable, no trailing slash, into buf.
 * False (buf empty) when the platform has no way to find it or it does not
 * fit: macOS _NSGetExecutablePath, Linux /proc/self/exe, Windows
 * GetModuleFileNameW (UTF-8 out), other: unknown. */
bool xrd_exepath_dir(char *buf, size_t len);

/* The directory part of a path ("/a/b/xrdome" -> "/a/b", "/xrdome" -> "/"),
 * into buf. False when there is no slash or it does not fit. On Windows both
 * slashes separate, and the root keeps its slash ("C:\x.exe" -> "C:\"). */
bool xrd_exepath_dirname(xrd_platform p, const char *path, char *buf, size_t len);

/* True when path is a regular file this process can execute (Windows: any
 * regular file; the name's .exe is what makes it a program there). */
bool xrd_exepath_is_exec(const char *path);

/* Picks the program to run as ffprobe, into out:
 *  1. configured, when it is not empty (the user's choice always wins);
 *  2. exe_dir/ffprobe (Windows: exe_dir\ffprobe.exe), when exe_dir is known, is_exec says it
 *     runs, and exe_dir is not itself one of the PATH entries (then PATH's own order applies);
 *  3. "ffprobe", which execvp looks up on PATH (Windows: CreateProcessW, which adds .exe).
 * is_exec is xrd_exepath_is_exec in the program and a stub in the tests.
 * False (out empty) when out is too small. */
bool xrd_exepath_pick_ffprobe(xrd_platform p, const char *configured, const char *exe_dir,
                              const char *path_env, bool (*is_exec)(const char *path), char *out,
                              size_t outlen);

/* The default for [general] ffprobe: pick_ffprobe with no configured value,
 * this executable's directory, getenv("PATH") and xrd_exepath_is_exec. */
void xrd_exepath_default_ffprobe(char *out, size_t outlen);

#endif
