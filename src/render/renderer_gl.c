#include "render/renderer_gl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "math/quat.h"
#include "render/gl.h"
#include "shaders_gen.h"

struct xrd_renderer {
    GLuint prog;
    GLuint vao;
    GLint u_video, u_rot, u_tan_half, u_proj, u_inv_fov, u_reject, u_flat_half, u_flat_curve,
        u_eye_scale, u_eye_offset, u_filter, u_dither, u_mask, u_trilinear, u_max_level,
        u_depth, u_depth_on, u_depth_k, u_depth_plane, u_depth_n;
    GLuint depth_tex; /* depth 3D: the picture's depth, 0 when there is none */
    int depth_w, depth_h; /* its size */
    /* Sharpening (sharpen.frag): its program, and the intermediate the eye
     * pass draws into while it is on, made on the first sharpened draw and
     * remade when the output size changes. */
    GLuint sharp_prog;
    GLint s_src, s_lo, s_hi, s_peak, s_dither;
    GLuint mid_fbo, mid_tex;
    int mid_w, mid_h;
    bool mid_failed; /* the intermediate could not be made: draw without sharpening */
    float hfov_deg, vfov_deg;
    float convergence_deg;
    xrd_filter filter;
    bool dither;
    float sharpen;
};

static void set_err(char *err, size_t errlen, const char *what, const char *log)
{
    if (err && errlen)
        snprintf(err, errlen, "%s: %s", what, log);
}

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
        glGetShaderInfoLog(s, (GLsizei)sizeof log, NULL, log);
        set_err(err, errlen, what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* Links vs and fs into a program, deleting both shaders; 0 on failure. */
static GLuint link_program(GLuint vs, GLuint fs, char *err, size_t errlen)
{
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
        return 0;
    }
    return prog;
}

/* The vertex shader vs_lines with fs_lines as the fragment shader. */
static GLuint build(const char *const *vs_lines, int vs_n, const char *const *fs_lines, int fs_n,
                    const char *fs_what, char *err, size_t errlen)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vs_lines, vs_n, "vr180.vert", err, errlen);
    if (!vs)
        return 0;
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_lines, fs_n, fs_what, err, errlen);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    return link_program(vs, fs, err, errlen);
}

xrd_renderer *xrd_renderer_new(char *err, size_t errlen)
{
    bool es = xrd_gl_is_es();
    GLuint prog = es ? build(XRD_SHADER_VR180_VERT_ES, XRD_SHADER_LINES(XRD_SHADER_VR180_VERT_ES),
                             XRD_SHADER_VR180_FRAG_ES, XRD_SHADER_LINES(XRD_SHADER_VR180_FRAG_ES),
                             "vr180.frag", err, errlen)
                     : build(XRD_SHADER_VR180_VERT, XRD_SHADER_LINES(XRD_SHADER_VR180_VERT),
                             XRD_SHADER_VR180_FRAG, XRD_SHADER_LINES(XRD_SHADER_VR180_FRAG),
                             "vr180.frag", err, errlen);
    if (!prog)
        return NULL;
    GLuint sharp = es ? build(XRD_SHADER_VR180_VERT_ES, XRD_SHADER_LINES(XRD_SHADER_VR180_VERT_ES),
                              XRD_SHADER_SHARPEN_FRAG_ES, XRD_SHADER_LINES(XRD_SHADER_SHARPEN_FRAG_ES),
                              "sharpen.frag", err, errlen)
                      : build(XRD_SHADER_VR180_VERT, XRD_SHADER_LINES(XRD_SHADER_VR180_VERT),
                              XRD_SHADER_SHARPEN_FRAG, XRD_SHADER_LINES(XRD_SHADER_SHARPEN_FRAG),
                              "sharpen.frag", err, errlen);
    if (!sharp) {
        glDeleteProgram(prog);
        return NULL;
    }

    xrd_renderer *r = calloc(1, sizeof *r);
    if (!r) {
        glDeleteProgram(prog);
        glDeleteProgram(sharp);
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
    r->u_proj = glGetUniformLocation(prog, "u_proj");
    r->u_flat_half = glGetUniformLocation(prog, "u_flat_half");
    r->u_flat_curve = glGetUniformLocation(prog, "u_flat_curve");
    r->u_eye_scale = glGetUniformLocation(prog, "u_eye_scale");
    r->u_eye_offset = glGetUniformLocation(prog, "u_eye_offset");
    r->u_filter = glGetUniformLocation(prog, "u_filter");
    r->convergence_deg = 0.f;
    r->filter = XRD_FILTER_BILINEAR;
    r->u_dither = glGetUniformLocation(prog, "u_dither");
    r->u_trilinear = glGetUniformLocation(prog, "u_trilinear");
    r->u_max_level = glGetUniformLocation(prog, "u_max_level");
    r->dither = false;
    r->u_mask = glGetUniformLocation(prog, "u_mask");
    r->u_depth = glGetUniformLocation(prog, "u_depth");
    r->u_depth_on = glGetUniformLocation(prog, "u_depth_on");
    r->u_depth_k = glGetUniformLocation(prog, "u_depth_k");
    r->u_depth_plane = glGetUniformLocation(prog, "u_depth_plane");
    r->u_depth_n = glGetUniformLocation(prog, "u_depth_n");
    r->sharp_prog = sharp;
    r->s_src = glGetUniformLocation(sharp, "u_src");
    r->s_lo = glGetUniformLocation(sharp, "u_lo");
    r->s_hi = glGetUniformLocation(sharp, "u_hi");
    r->s_peak = glGetUniformLocation(sharp, "u_peak");
    r->s_dither = glGetUniformLocation(sharp, "u_dither");
    r->sharpen = 0.f;
    return r;
}

void xrd_renderer_free(xrd_renderer *r)
{
    if (!r)
        return;
    glDeleteVertexArrays(1, &r->vao);
    glDeleteProgram(r->prog);
    glDeleteProgram(r->sharp_prog);
    if (r->mid_fbo)
        glDeleteFramebuffers(1, &r->mid_fbo);
    if (r->mid_tex)
        glDeleteTextures(1, &r->mid_tex);
    if (r->depth_tex)
        glDeleteTextures(1, &r->depth_tex);
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

void xrd_renderer_set_dither(xrd_renderer *r, bool on)
{
    r->dither = on;
}

void xrd_renderer_set_filter(xrd_renderer *r, xrd_filter f)
{
    r->filter = f;
}

void xrd_renderer_set_sharpen(xrd_renderer *r, float strength)
{
    r->sharpen = strength > 0.f ? (strength < 1.f ? strength : 1.f) : 0.f;
}

bool xrd_renderer_set_depth(xrd_renderer *r, const uint16_t *px, int w, int h, char *err,
                            size_t errlen)
{
    if (xrd_gl_is_es()) {
        /* GL_R16 needs an extension in GLES 3.0; NULL (drop) has nothing to drop. */
        if (!px)
            return true;
        if (err && errlen)
            snprintf(err, errlen, "depth 3D needs desktop GL");
        return false;
    }
    /* A new map of the size the texture has (every frame of a film) is
     * written into it, not made anew. */
    bool reuse = px && r->depth_tex && w == r->depth_w && h == r->depth_h;
    if (r->depth_tex && !reuse) {
        glDeleteTextures(1, &r->depth_tex);
        r->depth_tex = 0;
        r->depth_w = r->depth_h = 0;
    }
    if (!px)
        return true;
    GLint max = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    if (w < 1 || h < 1 || w > max || h > max) {
        char what[64];
        snprintf(what, sizeof what, "%dx%d", w, h);
        set_err(err, errlen, what, "outside the GPU's texture size limit");
        return false;
    }
    /* GL's rows run bottom up: the top row goes last, so that t = 1 is the
     * top, as in the video texture. */
    size_t row = (size_t)w;
    uint16_t *flip = malloc(row * (size_t)h * sizeof *flip);
    if (!flip) {
        set_err(err, errlen, "depth", "out of memory");
        return false;
    }
    for (int y = 0; y < h; y++)
        memcpy(flip + (size_t)(h - 1 - y) * row, px + (size_t)y * row, row * sizeof *flip);
    /* The unpack state is mpv's as much as ours: set what the upload needs
     * and put it back. */
    GLint align, row_len, skip_px, skip_rows, pbo;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_len);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_px);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    while (glGetError() != GL_NO_ERROR)
        ;
    if (reuse) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->depth_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_SHORT, flip);
    } else {
        glGenTextures(1, &r->depth_tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->depth_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, flip);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }
    GLenum e = glGetError();
    glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_len);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_px);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)pbo);
    free(flip);
    if (e != GL_NO_ERROR) {
        char what[64];
        snprintf(what, sizeof what, "%dx%d R16 texture", w, h);
        set_err(err, errlen, what, "GL error on upload");
        glDeleteTextures(1, &r->depth_tex);
        r->depth_tex = 0;
        r->depth_w = r->depth_h = 0;
        return false;
    }
    r->depth_w = w;
    r->depth_h = h;
    return true;
}

bool xrd_renderer_has_depth(const xrd_renderer *r)
{
    return r->depth_tex != 0;
}

/* Makes the intermediate w x h (GL_RGB10_A2: 10 bits per colour, the same
 * as the video texture, and 2 alpha bits for the picture mask), bound as the
 * draw framebuffer. false, once and for good, if the GPU cannot render to it;
 * the draw framebuffer binding is then undefined. */
static bool mid_ready(xrd_renderer *r, int w, int h)
{
    if (r->mid_failed)
        return false;
    if (!r->mid_fbo) {
        glGenTextures(1, &r->mid_tex);
        glGenFramebuffers(1, &r->mid_fbo);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->mid_fbo);
    if (r->mid_w == w && r->mid_h == h)
        return true;
    glBindTexture(GL_TEXTURE_2D, r->mid_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV,
                 NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->mid_tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "xrdome: warning: sharpening is off: the GPU cannot render to a "
                        "%dx%d 10-bit texture (framebuffer status 0x%x)\n", w, h, (unsigned)st);
        r->mid_failed = true;
        return false;
    }
    r->mid_w = w;
    r->mid_h = h;
    return true;
}

/* The shader's u_filter for f: 0 bilinear, else the enlarge() kernel. */
static int filter_index(xrd_filter f)
{
    switch (f) {
    case XRD_FILTER_BICUBIC:
        return 1;
    case XRD_FILTER_BICUBIC_AR:
        return 2;
    case XRD_FILTER_MITCHELL:
        return 3;
    default:
        return 0;
    }
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
                       const xrd_draw_params *dp, int out_w, int out_h)
{
    /* Own the fixed-function state the pass depends on. */
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    if (!xrd_gl_is_es())
        glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* With sharpening the eye pass draws into the intermediate, undithered
     * and with the picture mask in alpha, and sharpen.frag draws each eye
     * from it into the caller's framebuffer. Without it the eye pass draws
     * straight there, exactly as before sharpening existed. */
    glActiveTexture(GL_TEXTURE0);
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    bool sharpen = r->sharpen > 0.f && mid_ready(r, out_w, out_h);
    if (!sharpen)
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)target);

    glUseProgram(r->prog);
    glBindVertexArray(r->vao);
    glBindTexture(GL_TEXTURE_2D, video_tex);
    glUniform1i(r->u_video, 0);
    glUniform2f(r->u_tan_half, dp->geom.tan_h, dp->geom.tan_v);
    glUniform1i(r->u_proj, dp->proj == XRD_PROJ_FLAT ? 1 : 0);
    /* The shader draws black for a flat screen whose extents are not > 0. */
    glUniform2f(r->u_flat_half, dp->geom.half_w, dp->geom.half_h);
    /* and for a curve that is neither 0 (the plane) nor in range */
    glUniform1f(r->u_flat_curve, dp->screen_curve);
    const double deg = 3.14159265358979323846 / 180.0;
    glUniform2f(r->u_reject, r->hfov_deg < XRD_FULL_FOV_DEG ? 1.f : 0.f, r->vfov_deg < XRD_FULL_FOV_DEG ? 1.f : 0.f);
    glUniform2f(r->u_inv_fov, (float)(1.0 / (r->hfov_deg * deg)), (float)(1.0 / (r->vfov_deg * deg)));
    /* Trilinear replaces the enlarging filter: it is used only while shrinking. */
    glUniform1i(r->u_trilinear, dp->trilinear);
    glUniform1i(r->u_max_level, XRD_MIP_MAX_LEVEL);
    glUniform1i(r->u_filter, dp->trilinear ? 0 : filter_index(r->filter));
    glUniform1i(r->u_dither, r->dither && !sharpen);
    glUniform1i(r->u_mask, sharpen);
    /* Depth 3D: only for a flat mono picture; at strength 0 the draw is
     * exactly the one without it. */
    bool depth = dp->depth && r->depth_tex && dp->proj == XRD_PROJ_FLAT &&
                 dp->layout == XRD_LAYOUT_MONO && dp->depth_strength > 0.f;
    glUniform1i(r->u_depth_on, depth);
    glUniform1i(r->u_depth, 1);
    if (depth) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->depth_tex);
        glActiveTexture(GL_TEXTURE0);
        glUniform1f(r->u_depth_plane, dp->depth_plane);
        glUniform1i(r->u_depth_n, XRD_DEPTH_SEARCH_N);
    }

    int half = out_w / 2;
    for (int eye = 0; eye < 2; eye++) {
        float scale[2], offset[2];
        xrd_eye_uv_transform(dp->layout, eye, dp->swap_eyes, scale, offset);
        glUniform2f(r->u_eye_scale, scale[0], scale[1]);
        glUniform2f(r->u_eye_offset, offset[0], offset[1]);
        if (depth)
            glUniform1f(r->u_depth_k, xrd_depth_shift(eye, dp->swap_eyes, dp->depth_strength));
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
    if (!sharpen)
        return;

    /* CAS's negative lobe runs from -1/8 (its sharpness 0) to -1/5 (its
     * sharpness 1); strength 1 is the latter and smaller strengths scale it
     * down to 0, so the effect fades out continuously. */
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)target);
    glUseProgram(r->sharp_prog);
    glBindTexture(GL_TEXTURE_2D, r->mid_tex);
    glUniform1i(r->s_src, 0);
    glUniform1f(r->s_peak, -r->sharpen / 5.f);
    glUniform1i(r->s_dither, r->dither);
    for (int eye = 0; eye < 2; eye++) {
        glUniform2i(r->s_lo, eye * half, 0);
        glUniform2i(r->s_hi, eye * half + half - 1, out_h - 1);
        glViewport(eye * half, 0, half, out_h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}
