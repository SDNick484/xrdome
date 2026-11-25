/* The GL facts the device report lists (spec 5.3 Device report, H2). */
#ifndef XRD_ANDROID_GL_INFO_H
#define XRD_ANDROID_GL_INFO_H

#include <stddef.h>

/* Makes a 1x1 pbuffer surface and an ES 3.0 context current on the calling thread, reads
 * GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION and whether GL_OES_EGL_image_external_essl3
 * is listed, writes them as "NAME: value" lines into buf (always NUL-terminated, truncated to
 * size), and releases everything. On failure the text is one "error: <what>" line. Returns 0 on
 * success, -1 on failure. Leaves no context current. */
int xrd_gl_info(char *buf, size_t size);

#endif
