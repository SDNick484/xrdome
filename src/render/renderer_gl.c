#include "render/renderer_gl.h"

#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>

#include "math/quat.h"
#include "shaders_gen.h"

struct xrd_renderer {
    GLuint prog;
    GLuint vao;
    GLint u_video, u_rot, u_tan_half, u_inv_fov, u_reject, u_u_scale, u_u_offset, u_bicubic;
    float hfov_deg, vfov_deg;
    float convergence_deg;
    xrd_filter filter;
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
    r->u_inv_fov = glGetUniformLocation(prog, "u_inv_fov");
    r->u_reject = glGetUniformLocation(prog, "u_reject");
    r->hfov_deg = 180.f;
    r->vfov_deg = 180.f;
    r->u_u_scale = glGetUniformLocation(prog, "u_u_scale");
    r->u_u_offset = glGetUniformLocation(prog, "u_u_offset");
    r->u_bicubic = glGetUniformLocation(prog, "u_bicubic");
    r->convergence_deg = 0.f;
    r->filter = XRD_FILTER_BILINEAR;
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

void xrd_renderer_set_content_fov(xrd_renderer *r, float hfov_deg, float vfov_deg)
{
    r->hfov_deg = hfov_deg;
    r->vfov_deg = vfov_deg;
}

void xrd_renderer_set_convergence(xrd_renderer *r, float deg)
{
    r->convergence_deg = deg;
}

void xrd_renderer_set_filter(xrd_renderer *r, xrd_filter f)
{
    r->filter = f;
}

/* out = a * b, all column-major 3x3. */
static void mat3_mul(const float a[9], const float b[9], float out[9])
{
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++)
            out[c * 3 + row] = a[0 * 3 + row] * b[c * 3 + 0] + a[1 * 3 + row] * b[c * 3 + 1] +
                               a[2 * 3 + row] * b[c * 3 + 2];
}

void xrd_renderer_draw(xrd_renderer *r, unsigned video_tex, const float rot[9],
                       float tan_h, float tan_v, xrd_layout layout, bool swap_eyes,
                       int out_w, int out_h)
{
    /* Own the fixed-function state the pass depends on. */
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glUseProgram(r->prog);
    glBindVertexArray(r->vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, video_tex);
    glUniform1i(r->u_video, 0);
    glUniform2f(r->u_tan_half, tan_h, tan_v);
    const double deg = 3.14159265358979323846 / 180.0;
    glUniform2f(r->u_reject, r->hfov_deg < XRD_FULL_FOV_DEG ? 1.f : 0.f, r->vfov_deg < XRD_FULL_FOV_DEG ? 1.f : 0.f);
    glUniform2f(r->u_inv_fov, (float)(1.0 / (r->hfov_deg * deg)), (float)(1.0 / (r->vfov_deg * deg)));
    glUniform1i(r->u_bicubic, r->filter == XRD_FILTER_BICUBIC);

    int half = out_w / 2;
    for (int eye = 0; eye < 2; eye++) {
        float scale, offset;
        xrd_eye_u_transform(layout, eye, swap_eyes, &scale, &offset);
        glUniform1f(r->u_u_scale, scale);
        glUniform1f(r->u_u_offset, offset);
        /* Convergence, in head space after the view rotation: positive yaw
         * turns left, so the left viewport turns right by half (-c/2) and
         * the right viewport left (+c/2). */
        float eye_rot[9], eye_view[9];
        double half_c = r->convergence_deg / 2.0;
        xrd_quat_to_mat3(xrd_quat_from_ypr_deg(eye == 0 ? -half_c : half_c, 0, 0), eye_rot);
        mat3_mul(rot, eye_rot, eye_view);
        glUniformMatrix3fv(r->u_rot, 1, GL_FALSE, eye_view);
        glViewport(eye * half, 0, half, out_h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}
