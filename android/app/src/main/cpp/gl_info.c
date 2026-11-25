/* GL facts for the device report: a throwaway pbuffer ES 3.0 context (spec 5.3, H2). */
#include "gl_info.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>

static const char *gl_str(GLenum name)
{
    const char *s = (const char *)glGetString(name);
    return s ? s : "(null)";
}

/* True if the space-separated extension list has exactly this word. */
static int has_word(const char *list, const char *word)
{
    size_t n = strlen(word);
    for (const char *p = list; (p = strstr(p, word)) != NULL; p += n) {
        int start_ok = (p == list) || (p[-1] == ' ');
        int end_ok = (p[n] == ' ' || p[n] == '\0');
        if (start_ok && end_ok)
            return 1;
    }
    return 0;
}

static int fail(char *buf, size_t size, const char *what)
{
    snprintf(buf, size, "error: %s\n", what);
    return -1;
}

int xrd_gl_info(char *buf, size_t size)
{
    if (!buf || size == 0)
        return -1;
    buf[0] = '\0';

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
        return fail(buf, size, "eglInitialize failed");

    static const EGLint cfg_attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &ncfg) || ncfg < 1) {
        eglTerminate(dpy);
        return fail(buf, size, "no EGL config with ES 3.0 and a pbuffer");
    }

    static const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    static const EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
    if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE || !eglMakeCurrent(dpy, surf, surf, ctx)) {
        if (surf != EGL_NO_SURFACE)
            eglDestroySurface(dpy, surf);
        if (ctx != EGL_NO_CONTEXT)
            eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        return fail(buf, size, "could not make an ES 3.0 pbuffer context current");
    }

    const char *ext = gl_str(GL_EXTENSIONS);
    snprintf(buf, size,
             "GL_RENDERER: %s\n"
             "GL_VERSION: %s\n"
             "GL_SHADING_LANGUAGE_VERSION: %s\n"
             "GL_OES_EGL_image_external_essl3: %s\n",
             gl_str(GL_RENDERER), gl_str(GL_VERSION), gl_str(GL_SHADING_LANGUAGE_VERSION),
             has_word(ext, "GL_OES_EGL_image_external_essl3") ? "yes" : "no");

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
