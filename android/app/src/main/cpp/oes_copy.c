/* The copy pass: OES video frame -> GL_RGB10_A2 texture (spec 4.2 B, 5.3 GL). See oes_copy.h. */
#include "oes_copy.h"

#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>

#include "oes_copy_frag_gen.h" /* XRD_SHADER_OES_COPY_FRAG: oes_copy.frag, by CMakeLists.txt */
#include "shaders_gen.h"       /* XRD_SHADER_VR180_VERT_ES: the fullscreen triangle */

/* GL_OES_EGL_image_external; defined here rather than mixing in GLES2/gl2ext.h. */
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

struct xrd_oes_copy {
    GLuint prog, vao, fbo, tex;
    GLint u_src, u_st;
    int w, h;
};

static GLuint compile(GLenum type, const char *const *lines, int n, const char *what, char *err,
                      size_t errlen)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, n, lines, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        snprintf(err, errlen, "copy pass: %s does not compile: %s", what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

xrd_oes_copy *xrd_oes_copy_new(char *err, size_t errlen)
{
    GLuint vs = compile(GL_VERTEX_SHADER, XRD_SHADER_VR180_VERT_ES,
                        XRD_SHADER_LINES(XRD_SHADER_VR180_VERT_ES), "vr180.vert", err, errlen);
    if (!vs)
        return NULL;
    GLuint fs = compile(GL_FRAGMENT_SHADER, XRD_SHADER_OES_COPY_FRAG,
                        XRD_SHADER_LINES(XRD_SHADER_OES_COPY_FRAG), "oes_copy.frag", err, errlen);
    if (!fs) {
        glDeleteShader(vs);
        return NULL;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        snprintf(err, errlen, "copy pass: the program does not link: %s", log);
        glDeleteProgram(prog);
        return NULL;
    }

    xrd_oes_copy *c = calloc(1, sizeof *c);
    if (!c) {
        snprintf(err, errlen, "copy pass: out of memory");
        glDeleteProgram(prog);
        return NULL;
    }
    c->prog = prog;
    c->u_src = glGetUniformLocation(prog, "u_src");
    c->u_st = glGetUniformLocation(prog, "u_st");
    glGenVertexArrays(1, &c->vao); /* no attributes: the triangle comes from gl_VertexID */
    glGenFramebuffers(1, &c->fbo);
    return c;
}

static void drop_target(xrd_oes_copy *c)
{
    if (c->tex)
        glDeleteTextures(1, &c->tex);
    c->tex = 0;
    c->w = c->h = 0;
}

void xrd_oes_copy_free(xrd_oes_copy *c)
{
    if (!c)
        return;
    drop_target(c);
    glDeleteFramebuffers(1, &c->fbo);
    glDeleteVertexArrays(1, &c->vao);
    glDeleteProgram(c->prog);
    free(c);
}

/* (Re)allocates the w x h target. glTexStorage2D makes it immutable, so a new size is a new
 * texture. */
static bool alloc_target(xrd_oes_copy *c, int w, int h, char *err, size_t errlen)
{
    drop_target(c);
    glGenTextures(1, &c->tex);
    glBindTexture(GL_TEXTURE_2D, c->tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB10_A2, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, c->fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        snprintf(err, errlen, "copy pass: a %dx%d GL_RGB10_A2 target is not renderable (0x%x)",
                 w, h, (unsigned)st);
        drop_target(c);
        return false;
    }
    c->w = w;
    c->h = h;
    return true;
}

bool xrd_oes_copy_draw(xrd_oes_copy *c, unsigned oes_tex, const float st[16], int w, int h,
                       char *err, size_t errlen)
{
    if (w <= 0 || h <= 0) {
        snprintf(err, errlen, "copy pass: no video size (%dx%d)", w, h);
        drop_target(c);
        return false;
    }
    if ((w != c->w || h != c->h || !c->tex) && !alloc_target(c, w, h, err, errlen))
        return false;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, c->fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(c->prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oes_tex);
    glUniform1i(c->u_src, 0);
    glUniformMatrix4fv(c->u_st, 1, GL_FALSE, st);
    glBindVertexArray(c->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    return true;
}

unsigned xrd_oes_copy_texture(const xrd_oes_copy *c)
{
    return c->tex;
}

void xrd_oes_copy_size(const xrd_oes_copy *c, int *w, int *h)
{
    *w = c->w;
    *h = c->h;
}
