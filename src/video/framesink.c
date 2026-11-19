#define _POSIX_C_SOURCE 200809L /* shm_open, ftruncate, mmap */

#include "video/framesink.h"

#include <epoxy/gl.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows: no POSIX shared memory and no depth helper to read it
 * (app/depthclient.h), so a ring cannot be created and films stay 2D. */
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "framesink_gen.h"

#define PAGE 4096u

/* ---- the ring ------------------------------------------------------------------ */

static size_t round_page(size_t n)
{
    return (n + PAGE - 1) / PAGE * PAGE;
}

#define FRAME_STRIDE round_page(XRD_VRING_FRAME_BYTES)
#define DEPTH_STRIDE round_page(XRD_VRING_DEPTH_BYTES)

size_t xrd_vring_frame_offset(unsigned slot)
{
    return XRD_VRING_HEADER + (size_t)slot * FRAME_STRIDE;
}

size_t xrd_vring_depth_offset(unsigned slot)
{
    return xrd_vring_frame_offset(XRD_VRING_FRAME_SLOTS) + (size_t)slot * DEPTH_STRIDE;
}

size_t xrd_vring_size(void)
{
    return xrd_vring_depth_offset(XRD_VRING_DEPTH_SLOTS);
}

/* The 64-bit fields both processes touch, as atomics in the mapping. */
static _Atomic uint64_t *u64_at(const void *mem, size_t off)
{
    return (_Atomic uint64_t *)((uintptr_t)mem + off);
}

static uint64_t load(const void *mem, size_t off, memory_order mo)
{
    return atomic_load_explicit(u64_at(mem, off), mo);
}

static void store(void *mem, size_t off, uint64_t v, memory_order mo)
{
    atomic_store_explicit(u64_at(mem, off), v, mo);
}

static void put32(uint8_t *h, size_t off, uint32_t v)
{
    memcpy(h + off, &v, 4);
}

static void put64(uint8_t *h, size_t off, uint64_t v)
{
    memcpy(h + off, &v, 8);
}

static uint32_t get32(const uint8_t *h, size_t off)
{
    uint32_t v;
    memcpy(&v, h + off, 4);
    return v;
}

static uint64_t get64(const uint8_t *h, size_t off)
{
    uint64_t v;
    memcpy(&v, h + off, 8);
    return v;
}

void xrd_vring_init(void *mem)
{
    uint8_t *h = mem;
    memset(h, 0, XRD_VRING_HEADER);
    memcpy(h, XRD_VRING_MAGIC, 8);
    put32(h, 8, XRD_VRING_VERSION);
    put32(h, 12, XRD_VRING_HEADER);
    put32(h, 16, XRD_VRING_W);
    put32(h, 20, XRD_VRING_H);
    put32(h, 24, XRD_VRING_FRAME_SLOTS);
    put32(h, 28, XRD_VRING_DEPTH_SLOTS);
    put64(h, 32, xrd_vring_frame_offset(0));
    put64(h, 40, FRAME_STRIDE);
    put64(h, 48, xrd_vring_depth_offset(0));
    put64(h, 56, DEPTH_STRIDE);
    put64(h, 64, xrd_vring_size());
    put32(h, 72, XRD_VRING_FORMAT_RGBA8);
    put32(h, 76, XRD_VRING_FORMAT_U16);
    atomic_thread_fence(memory_order_release);
}

bool xrd_vring_valid(const void *mem, size_t len)
{
    const uint8_t *h = mem;
    return len >= xrd_vring_size() && memcmp(h, XRD_VRING_MAGIC, 8) == 0 &&
           get32(h, 8) == XRD_VRING_VERSION && get32(h, 12) == XRD_VRING_HEADER &&
           get32(h, 16) == XRD_VRING_W && get32(h, 20) == XRD_VRING_H &&
           get32(h, 24) == XRD_VRING_FRAME_SLOTS && get32(h, 28) == XRD_VRING_DEPTH_SLOTS &&
           get64(h, 32) == xrd_vring_frame_offset(0) && get64(h, 40) == FRAME_STRIDE &&
           get64(h, 48) == xrd_vring_depth_offset(0) && get64(h, 56) == DEPTH_STRIDE &&
           get64(h, 64) == xrd_vring_size() && get32(h, 72) == XRD_VRING_FORMAT_RGBA8 &&
           get32(h, 76) == XRD_VRING_FORMAT_U16;
}

static size_t frame_meta(uint64_t n)
{
    return XRD_VRING_OFF_FRAME_META + 16u * (size_t)(n % XRD_VRING_FRAME_SLOTS);
}

static size_t depth_meta(uint64_t m)
{
    return XRD_VRING_OFF_DEPTH_META + 16u * (size_t)(m % XRD_VRING_DEPTH_SLOTS);
}

/* The seqlock's writer: the slot reads 0 before any of its data changes. */
static void slot_begin(void *mem, size_t meta)
{
    store(mem, meta, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
}

/* And its reader: n in the slot before and after the copy. */
static bool slot_read(const void *mem, size_t meta, uint64_t n, void *dst, const void *src,
                      size_t bytes)
{
    if (load(mem, meta, memory_order_acquire) != n)
        return false;
    if (dst)
        memcpy(dst, src, bytes);
    atomic_thread_fence(memory_order_acquire);
    return load(mem, meta, memory_order_relaxed) == n;
}

uint8_t *xrd_vring_frame_begin(void *mem, uint64_t n)
{
    slot_begin(mem, frame_meta(n));
    return (uint8_t *)mem + xrd_vring_frame_offset((unsigned)(n % XRD_VRING_FRAME_SLOTS));
}

void xrd_vring_frame_commit(void *mem, uint64_t n, uint64_t t_ns)
{
    store(mem, frame_meta(n) + 8, t_ns, memory_order_relaxed);
    store(mem, frame_meta(n), n, memory_order_release);
    store(mem, XRD_VRING_OFF_FRAME_HEAD, n, memory_order_release);
}

uint64_t xrd_vring_frame_head(const void *mem)
{
    return load(mem, XRD_VRING_OFF_FRAME_HEAD, memory_order_acquire);
}

void xrd_vring_close(void *mem)
{
    atomic_store_explicit((_Atomic uint32_t *)((uintptr_t)mem + XRD_VRING_OFF_CLOSED), 1u,
                          memory_order_release);
}

bool xrd_vring_closed(const void *mem)
{
    return atomic_load_explicit((_Atomic uint32_t *)((uintptr_t)mem + XRD_VRING_OFF_CLOSED),
                                memory_order_acquire) != 0;
}

uint64_t xrd_vring_depth_head(const void *mem)
{
    return load(mem, XRD_VRING_OFF_DEPTH_HEAD, memory_order_acquire);
}

bool xrd_vring_depth_read(const void *mem, uint64_t *seen, uint16_t *dst, uint64_t *frame)
{
    uint64_t m = xrd_vring_depth_head(mem);
    if (m == 0 || m == *seen)
        return false;
    size_t meta = depth_meta(m);
    uint64_t f = load(mem, meta + 8, memory_order_relaxed);
    const uint8_t *src =
        (const uint8_t *)mem + xrd_vring_depth_offset((unsigned)(m % XRD_VRING_DEPTH_SLOTS));
    if (!slot_read(mem, meta, m, dst, src, XRD_VRING_DEPTH_BYTES))
        return false;
    *seen = m;
    if (frame)
        *frame = f;
    return true;
}

bool xrd_vring_frame_read(const void *mem, uint64_t *seen, uint8_t *dst, uint64_t *t_ns)
{
    uint64_t n = xrd_vring_frame_head(mem);
    if (n == 0 || n == *seen)
        return false;
    size_t meta = frame_meta(n);
    uint64_t t = load(mem, meta + 8, memory_order_relaxed);
    const uint8_t *src =
        (const uint8_t *)mem + xrd_vring_frame_offset((unsigned)(n % XRD_VRING_FRAME_SLOTS));
    if (!slot_read(mem, meta, n, dst, src, XRD_VRING_FRAME_BYTES))
        return false;
    *seen = n;
    if (t_ns)
        *t_ns = t;
    return true;
}

void xrd_vring_depth_write(void *mem, uint64_t m, uint64_t frame, const uint16_t *px)
{
    size_t meta = depth_meta(m);
    slot_begin(mem, meta);
    memcpy((uint8_t *)mem + xrd_vring_depth_offset((unsigned)(m % XRD_VRING_DEPTH_SLOTS)), px,
           XRD_VRING_DEPTH_BYTES);
    store(mem, meta + 8, frame, memory_order_relaxed);
    store(mem, meta, m, memory_order_release);
    store(mem, XRD_VRING_OFF_DEPTH_HEAD, m, memory_order_release);
}

/* ---- shared memory -------------------------------------------------------------- */

int xrd_vring_shm_name(char *buf, size_t len, long pid, unsigned n)
{
    int k = snprintf(buf, len, "/xrdome-%ld-%u", pid, n);
    return k < 0 || (size_t)k >= len || k > 31 ? -1 : 0;
}

#ifdef _WIN32

void *xrd_vring_shm_create(const char *name, char *err, size_t errlen)
{
    (void)name;
    snprintf(err, errlen, "shared memory for the depth helper is not available on Windows");
    return NULL;
}

void xrd_vring_shm_destroy(void *mem, const char *name)
{
    (void)mem;
    (void)name;
}

#else

void *xrd_vring_shm_create(const char *name, char *err, size_t errlen)
{
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0 && errno == EEXIST) { /* a ring a crashed xrdome of the same pid left */
        shm_unlink(name);
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    }
    if (fd < 0) {
        snprintf(err, errlen, "cannot create shared memory %s: %s", name, strerror(errno));
        return NULL;
    }
    size_t size = xrd_vring_size();
    void *mem = MAP_FAILED;
    if (ftruncate(fd, (off_t)size) == 0)
        mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int e = errno;
    close(fd);
    if (mem == MAP_FAILED) {
        shm_unlink(name);
        snprintf(err, errlen, "cannot map shared memory %s (%zu bytes): %s", name, size,
                 strerror(e));
        return NULL;
    }
    xrd_vring_init(mem);
    return mem;
}

void xrd_vring_shm_destroy(void *mem, const char *name)
{
    if (!mem)
        return;
    xrd_vring_close(mem);
    munmap(mem, xrd_vring_size());
    shm_unlink(name); /* ENOENT once the helper has taken the name away */
}

#endif /* !_WIN32 */

bool xrd_framesink_wanted(bool image, xrd_proj proj, xrd_layout layout, bool depth_on,
                          bool have_helper)
{
    return !image && proj == XRD_PROJ_FLAT && layout == XRD_LAYOUT_MONO && depth_on &&
           have_helper;
}

/* ---- the GL side ------------------------------------------------------------------ */

typedef struct {
    GLuint buf;
    GLsync fence; /* a readback in flight; 0 when the buffer is free */
    uint64_t order; /* when it was started, for oldest first */
} pbo;

struct xrd_framesink {
    char name[64];
    void *ring;
    GLuint fbo, tex, prog, vao;
    GLint u_out;
    pbo pb[XRD_FRAMESINK_PBOS];
    unsigned long last_serial;
    bool have_serial;
    uint64_t next_n; /* the next frame number in the ring */
    xrd_framesink_stats st;
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
        snprintf(err, errlen, "frame sink: %s: %s", what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint build_program(char *err, size_t errlen)
{
    GLuint vs = compile(GL_VERTEX_SHADER, XRD_SHADER_FRAMESINK_VERT,
                        XRD_SHADER_LINES(XRD_SHADER_FRAMESINK_VERT), "vr180.vert", err, errlen);
    if (!vs)
        return 0;
    GLuint fs = compile(GL_FRAGMENT_SHADER, XRD_SHADER_FRAMESINK_FRAG,
                        XRD_SHADER_LINES(XRD_SHADER_FRAMESINK_FRAG), "framesink.frag", err,
                        errlen);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        snprintf(err, errlen, "frame sink: link: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

xrd_framesink *xrd_framesink_new(const char *shm_name, char *err, size_t errlen)
{
    if (strlen(shm_name) >= sizeof ((xrd_framesink *)0)->name) {
        snprintf(err, errlen, "frame sink: shared memory name too long");
        return NULL;
    }
    xrd_framesink *fs = calloc(1, sizeof *fs);
    if (!fs) {
        snprintf(err, errlen, "frame sink: out of memory");
        return NULL;
    }
    snprintf(fs->name, sizeof fs->name, "%s", shm_name);
    fs->next_n = 1;
    if (!(fs->prog = build_program(err, errlen)))
        goto fail;
    fs->u_out = glGetUniformLocation(fs->prog, "u_out");
    glUseProgram(fs->prog);
    glUniform1i(glGetUniformLocation(fs->prog, "u_src"), 0);
    glGenVertexArrays(1, &fs->vao);

    glGenTextures(1, &fs->tex);
    glBindTexture(GL_TEXTURE_2D, fs->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, XRD_VRING_W, XRD_VRING_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &fs->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fs->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fs->tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        snprintf(err, errlen, "frame sink: %dx%d FBO incomplete (0x%x)", XRD_VRING_W,
                 XRD_VRING_H, status);
        goto fail;
    }
    /* GL_STREAM_READ: the driver keeps the buffers where the CPU reads them
     * fast (cached system memory), and the copy into them is the GPU's. */
    for (int i = 0; i < XRD_FRAMESINK_PBOS; i++) {
        glGenBuffers(1, &fs->pb[i].buf);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, fs->pb[i].buf);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)XRD_VRING_FRAME_BYTES, NULL,
                     GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!(fs->ring = xrd_vring_shm_create(shm_name, err, errlen)))
        goto fail;
    return fs;
fail:
    xrd_framesink_free(fs);
    return NULL;
}

void xrd_framesink_free(xrd_framesink *fs)
{
    if (!fs)
        return;
    xrd_vring_shm_destroy(fs->ring, fs->name);
    for (int i = 0; i < XRD_FRAMESINK_PBOS; i++) {
        if (fs->pb[i].fence)
            glDeleteSync(fs->pb[i].fence);
        if (fs->pb[i].buf)
            glDeleteBuffers(1, &fs->pb[i].buf);
    }
    if (fs->fbo)
        glDeleteFramebuffers(1, &fs->fbo);
    if (fs->tex)
        glDeleteTextures(1, &fs->tex);
    if (fs->vao)
        glDeleteVertexArrays(1, &fs->vao);
    if (fs->prog)
        glDeleteProgram(fs->prog);
    free(fs);
}

const char *xrd_framesink_shm_name(const xrd_framesink *fs)
{
    return fs->name;
}

void *xrd_framesink_ring(const xrd_framesink *fs)
{
    return fs->ring;
}

void xrd_framesink_get_stats(const xrd_framesink *fs, xrd_framesink_stats *out)
{
    *out = fs->st;
}

/* The oldest readback the GPU has finished, or -1. A zero timeout: this
 * only asks. */
static int finished(xrd_framesink *fs)
{
    int best = -1;
    for (int i = 0; i < XRD_FRAMESINK_PBOS; i++) {
        pbo *b = &fs->pb[i];
        if (!b->fence || (best >= 0 && b->order > fs->pb[best].order))
            continue;
        GLenum r = glClientWaitSync(b->fence, 0, 0);
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED)
            best = i;
    }
    return best;
}

/* Copies finished readbacks into the ring, oldest first. A later one
 * finishing before an earlier one is not expected (one GL queue), but the
 * earlier is then simply copied on a later frame, after it: the ring's
 * numbers follow the order of copying. */
static void harvest(xrd_framesink *fs, uint64_t now_ns)
{
    int i;
    while ((i = finished(fs)) >= 0) {
        pbo *b = &fs->pb[i];
        glDeleteSync(b->fence);
        b->fence = 0;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, b->buf);
        const void *px = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                                          (GLsizeiptr)XRD_VRING_FRAME_BYTES, GL_MAP_READ_BIT);
        if (px) {
            uint64_t n = fs->next_n++;
            memcpy(xrd_vring_frame_begin(fs->ring, n), px, XRD_VRING_FRAME_BYTES);
            xrd_vring_frame_commit(fs->ring, n, now_ns);
            fs->st.sent++;
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void xrd_framesink_step(xrd_framesink *fs, unsigned video_tex, unsigned long serial,
                        uint64_t now_ns)
{
    harvest(fs, now_ns);
    if (!video_tex || (fs->have_serial && serial == fs->last_serial))
        return;
    fs->last_serial = serial;
    fs->have_serial = true;
    int free_i = -1;
    for (int i = 0; i < XRD_FRAMESINK_PBOS && free_i < 0; i++)
        if (!fs->pb[i].fence)
            free_i = i;
    if (free_i < 0) {
        fs->st.skipped++;
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fs->fbo);
    glViewport(0, 0, XRD_VRING_W, XRD_VRING_H);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(fs->prog);
    glUniform2f(fs->u_out, (float)XRD_VRING_W, (float)XRD_VRING_H);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, video_tex);
    glBindVertexArray(fs->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    pbo *b = &fs->pb[free_i];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fs->fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, b->buf);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, XRD_VRING_W, XRD_VRING_H, GL_RGBA, GL_UNSIGNED_BYTE, (void *)0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    b->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    b->order = fs->st.started++;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* Without a flush the fence may never reach the GPU while the loop only
     * polls it; the swap flushes too, but not every caller swaps. */
    glFlush();
}
