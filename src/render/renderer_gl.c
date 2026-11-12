#include "render/renderer_gl.h"

#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>

#include "shaders_gen.h"

struct xrd_renderer {
    GLuint prog;
    GLuint vao;
    GLint u_video, u_rot, u_tan_half, u_u_scale, u_u_offset;
};

static void set_err(char *err, size_t errlen, const char *what, const char *log)
{
    if (err && errlen)
        snprintf(err, errlen, "%s: %s", what, log);
}

static GLuint compile(GLenum type, const char *src, const char *what, char *err, size_t errlen)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetShaderInfoLog(s, (GLsizei)sizeof log, NULL, log);
        set_err(err, errlen, what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

xrd_renderer *xrd_renderer_new(char *err, size_t errlen)
{
    GLuint vs = compile(GL_VERTEX_SHADER, XRD_SHADER_VR180_VERT, "vr180.vert", err, errlen);
    if (!vs)
        return NULL;
    GLuint fs = compile(GL_FRAGMENT_SHADER, XRD_SHADER_VR180_FRAG, "vr180.frag", err, errlen);
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
        glGetProgramInfoLog(prog, (GLsizei)sizeof log, NULL, log);
        set_err(err, errlen, "link", log);
        glDeleteProgram(prog);
        return NULL;
    }

    xrd_renderer *r = calloc(1, sizeof *r);
    if (!r) {
        glDeleteProgram(prog);
        set_err(err, errlen, "alloc", "out of memory");
        return NULL;
    }
    r->prog = prog;
    glGenVertexArrays(1, &r->vao);
    r->u_video = glGetUniformLocation(prog, "u_video");
    r->u_rot = glGetUniformLocation(prog, "u_rot");
    r->u_tan_half = glGetUniformLocation(prog, "u_tan_half");
    r->u_u_scale = glGetUniformLocation(prog, "u_u_scale");
    r->u_u_offset = glGetUniformLocation(prog, "u_u_offset");
    return r;
}

void xrd_renderer_free(xrd_renderer *r)
{
    if (!r)
        return;
    glDeleteVertexArrays(1, &r->vao);
    glDeleteProgram(r->prog);
    free(r);
}

void xrd_renderer_draw(xrd_renderer *r, unsigned video_tex, const float rot[9],
                       float tan_h, float tan_v, xrd_layout layout, bool swap_eyes,
                       int out_w, int out_h)
{
    glUseProgram(r->prog);
    glBindVertexArray(r->vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, video_tex);
    glUniform1i(r->u_video, 0);
    glUniformMatrix3fv(r->u_rot, 1, GL_FALSE, rot);
    glUniform2f(r->u_tan_half, tan_h, tan_v);

    int half = out_w / 2;
    for (int eye = 0; eye < 2; eye++) {
        float scale, offset;
        xrd_eye_u_transform(layout, eye, swap_eyes, &scale, &offset);
        glUniform1f(r->u_u_scale, scale);
        glUniform1f(r->u_u_offset, offset);
        glViewport(eye * half, 0, half, out_h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}
