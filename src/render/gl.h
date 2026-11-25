#ifndef XRD_RENDER_GL_H
#define XRD_RENDER_GL_H

/* The GL API the renderer compiles against: GLES 3.0 on Android, libepoxy
 * (desktop GL 3.3 core, or GLES when the context is one) elsewhere. */

#include <stdbool.h>

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <epoxy/gl.h>
#endif

/* True when the current context is OpenGL ES (the renderer then compiles the
 * ESSL 3.00 shaders). Android: always. Elsewhere: !epoxy_is_desktop_gl(). */

#ifdef __ANDROID__
static inline bool xrd_gl_is_es(void) { return true; }
#else
static inline bool xrd_gl_is_es(void) { return !epoxy_is_desktop_gl(); }
#endif

#endif
