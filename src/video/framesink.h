#ifndef XRD_VIDEO_FRAMESINK_H
#define XRD_VIDEO_FRAMESINK_H

/* Video frames out to the depth helper (spec 8 of
 * docs/superpowers/specs/2026-11-19-depth-3d-design.md, first half): each
 * new frame of a flat mono film with 3D on is scaled on the GPU to 322x182
 * into a small FBO, read back asynchronously into a pixel buffer (PBO) and,
 * once the GPU has finished it (a later frame; never waited for), copied
 * into a shared-memory ring that `xrdome-tools depth serve` maps (its
 * "video" op). The helper writes depth maps, with the number of the frame
 * each was made from, back into the same ring.
 *
 * The ring (tools/xrdome_depth/video.py mirrors it; native byte order, both
 * processes run on one machine):
 *
 *   offset  size  field
 *   0       8     magic "XRDVRNG1"
 *   8       4     version (1)
 *   12      4     header bytes (4096): the data starts after it
 *   16      4     width (322)        20  4  height (182)
 *   24      4     frame slots (4)    28  4  depth slots (3)
 *   32      8     offset of frame slot 0      40  8  bytes between frame slots
 *   48      8     offset of depth slot 0      56  8  bytes between depth slots
 *   64      8     total bytes of the ring
 *   72      4     frame format: 1 = RGBA8, 4 bytes a pixel (alpha unused),
 *                 rows top first, no padding
 *   76      4     depth format: 1 = 16-bit unsigned, 0 far, 65535 near,
 *                 rows top first, no padding
 *   128     8     frame head: the number of the newest whole frame (player)
 *   136     4     closed: 1 once the player has stopped sending (player)
 *   192     8     depth head: the number of the newest whole depth map (helper)
 *   256     16*i  frame slot i: its frame number (0 while being written),
 *                 and the player's CLOCK_MONOTONIC ns when it was copied in
 *   512     16*i  depth slot i: its depth map number (0 while being
 *                 written), and the number of the frame it was made from
 *
 * Frames are numbered 1, 2, ... in the order sent and frame n lives in slot
 * n % frame slots; depth maps likewise. A writer marks the slot 0, writes
 * it, then stores n in the slot and in the head. A reader takes the head n,
 * checks the slot holds n, copies it, and checks the slot still holds n:
 * otherwise the writer came round to the slot during the copy and the copy
 * is thrown away (a seqlock). Nobody ever waits for the other side. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "render/projection.h"

#define XRD_VRING_MAGIC "XRDVRNG1"
#define XRD_VRING_VERSION 1u
#define XRD_VRING_W 322
#define XRD_VRING_H 182
#define XRD_VRING_FRAME_SLOTS 4u
#define XRD_VRING_DEPTH_SLOTS 3u
#define XRD_VRING_HEADER 4096u
#define XRD_VRING_FRAME_BYTES ((size_t)XRD_VRING_W * XRD_VRING_H * 4u)
#define XRD_VRING_DEPTH_BYTES ((size_t)XRD_VRING_W * XRD_VRING_H * 2u)
#define XRD_VRING_FORMAT_RGBA8 1u
#define XRD_VRING_FORMAT_U16 1u

/* Header offsets (see the table above). */
#define XRD_VRING_OFF_FRAME_HEAD 128u
#define XRD_VRING_OFF_CLOSED 136u
#define XRD_VRING_OFF_DEPTH_HEAD 192u
#define XRD_VRING_OFF_FRAME_META 256u
#define XRD_VRING_OFF_DEPTH_META 512u

/* Data offsets: each slot starts on a 4096-byte boundary. */
size_t xrd_vring_frame_offset(unsigned slot);
size_t xrd_vring_depth_offset(unsigned slot);
/* Total bytes of the ring. */
size_t xrd_vring_size(void);

/* Writes the header into mem (xrd_vring_size bytes, zeroed or not): no
 * frame, no depth, open. */
void xrd_vring_init(void *mem);
/* True when mem (len bytes) holds a ring of this layout. */
bool xrd_vring_valid(const void *mem, size_t len);

/* Player side. Frame n (> 0, one more than the last): begin marks its slot
 * as being written and returns the slot's pixels (XRD_VRING_FRAME_BYTES);
 * commit publishes it. */
uint8_t *xrd_vring_frame_begin(void *mem, uint64_t n);
void xrd_vring_frame_commit(void *mem, uint64_t n, uint64_t t_ns);
uint64_t xrd_vring_frame_head(const void *mem);
void xrd_vring_close(void *mem);
bool xrd_vring_closed(const void *mem);

/* The newest depth map, when it is newer than *seen: copied into dst
 * (XRD_VRING_W x XRD_VRING_H samples; NULL only checks), its number into
 * *seen and the frame it was made from into *frame. False when there is
 * nothing newer or the helper overwrote the slot during the copy (try again
 * next frame). */
bool xrd_vring_depth_read(const void *mem, uint64_t *seen, uint16_t *dst, uint64_t *frame);
uint64_t xrd_vring_depth_head(const void *mem);

/* Helper side, in C for the tests (video.py is the real one): the newest
 * frame when newer than *seen, as xrd_vring_depth_read; and depth map m
 * (one more than the last) made from frame. */
bool xrd_vring_frame_read(const void *mem, uint64_t *seen, uint8_t *dst, uint64_t *t_ns);
void xrd_vring_depth_write(void *mem, uint64_t m, uint64_t frame, const uint16_t *px);

/* "/xrdome-<pid>-<n>": a POSIX shared-memory name, short enough for macOS
 * (31 bytes). -1 when it does not fit len. */
int xrd_vring_shm_name(char *buf, size_t len, long pid, unsigned n);
/* Creates the named shared-memory object (mode 0600; a stale one of the same
 * name is replaced), sizes it, maps it and initialises the ring. NULL with
 * err set on failure (nothing is left behind). */
void *xrd_vring_shm_create(const char *name, char *err, size_t errlen);
/* Marks the ring closed, unmaps it and removes the name (the helper may
 * already have removed it; its mapping lives on until it unmaps). NULL is
 * fine. */
void xrd_vring_shm_destroy(void *mem, const char *name);

/* Whether frames go out: a film (not a photo), flat mono, with 3D on, and a
 * depth helper to send them to. Everything else reads nothing back. */
bool xrd_framesink_wanted(bool image, xrd_proj proj, xrd_layout layout, bool depth_on,
                          bool have_helper);

/* ---- the GL side ------------------------------------------------------------ */

#define XRD_FRAMESINK_PBOS 3

typedef struct xrd_framesink xrd_framesink;

/* Creates the ring as xrd_vring_shm_create(shm_name) and the GL objects (a
 * 322x182 RGBA8 FBO, the downscale program, XRD_FRAMESINK_PBOS pixel
 * buffers). Needs a current GL 3.3 core context. NULL with err on failure. */
xrd_framesink *xrd_framesink_new(const char *shm_name, char *err, size_t errlen);
/* Closes and removes the ring and deletes the GL objects (the context must be
 * current). Readbacks still in flight are dropped. NULL is fine. */
void xrd_framesink_free(xrd_framesink *fs);
const char *xrd_framesink_shm_name(const xrd_framesink *fs);
/* The ring itself (xrd_vring_*), for reading the depth back. */
void *xrd_framesink_ring(const xrd_framesink *fs);

/* Once per loop iteration, never waiting for the GPU: first every readback
 * the GPU has finished (its fence signalled) is copied into the ring as the
 * next frame, oldest first; then, when serial differs from the one last
 * sent and video_tex is not 0, video_tex (GL-oriented, any size, level 0
 * read) is scaled into the FBO and its readback started into a free pixel
 * buffer. With every buffer still in flight that frame is skipped (counted).
 * Changes the draw and read framebuffer, viewport, program, VAO, active
 * texture unit and its 2D texture, and the pack buffer binding; the
 * framebuffers and pack buffer are left at 0. now_ns: CLOCK_MONOTONIC. */
void xrd_framesink_step(xrd_framesink *fs, unsigned video_tex, unsigned long serial,
                        uint64_t now_ns);

typedef struct {
    uint64_t started;  /* readbacks started */
    uint64_t sent;     /* frames copied into the ring */
    uint64_t skipped;  /* new frames dropped: every pixel buffer was in flight */
} xrd_framesink_stats;
void xrd_framesink_get_stats(const xrd_framesink *fs, xrd_framesink_stats *out);

#endif
