#define _POSIX_C_SOURCE 200809L /* shm_open, getpid */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cmocka.h>

#include "video/framesink.h"

/* The ring in ordinary memory, 8-byte aligned as a mapping is. */
static void *new_ring(void)
{
    void *mem = NULL;
    assert_int_equal(posix_memalign(&mem, 4096, xrd_vring_size()), 0);
    xrd_vring_init(mem);
    return mem;
}

static uint32_t u32(const void *mem, size_t off)
{
    uint32_t v;
    memcpy(&v, (const uint8_t *)mem + off, 4);
    return v;
}

static uint64_t u64(const void *mem, size_t off)
{
    uint64_t v;
    memcpy(&v, (const uint8_t *)mem + off, 8);
    return v;
}

static void layout_is_the_documented_one(void **state)
{
    (void)state;
    /* tools/xrdome_depth/video.py and tools/tests/test_depth_video.py hold the same numbers. */
    assert_int_equal(XRD_VRING_FRAME_BYTES, 322 * 182 * 4);
    assert_int_equal(XRD_VRING_DEPTH_BYTES, 322 * 182 * 2);
    assert_int_equal(xrd_vring_frame_offset(0), 4096);
    assert_int_equal(xrd_vring_frame_offset(1), 4096 + 237568);
    assert_int_equal(xrd_vring_depth_offset(0), 4096 + 4 * 237568);
    assert_int_equal(xrd_vring_depth_offset(1), 4096 + 4 * 237568 + 118784);
    assert_int_equal(xrd_vring_size(), 1310720);

    void *mem = new_ring();
    assert_memory_equal(mem, "XRDVRNG1", 8);
    assert_int_equal(u32(mem, 8), 1);
    assert_int_equal(u32(mem, 12), 4096);
    assert_int_equal(u32(mem, 16), 322);
    assert_int_equal(u32(mem, 20), 182);
    assert_int_equal(u32(mem, 24), 4);
    assert_int_equal(u32(mem, 28), 3);
    assert_int_equal(u64(mem, 32), 4096);
    assert_int_equal(u64(mem, 40), 237568);
    assert_int_equal(u64(mem, 48), 4096 + 4 * 237568);
    assert_int_equal(u64(mem, 56), 118784);
    assert_int_equal(u64(mem, 64), 1310720);
    assert_int_equal(u32(mem, 72), 1);
    assert_int_equal(u32(mem, 76), 1);
    assert_true(xrd_vring_valid(mem, xrd_vring_size()));
    assert_false(xrd_vring_valid(mem, xrd_vring_size() - 1));
    assert_int_equal(xrd_vring_frame_head(mem), 0);
    assert_int_equal(xrd_vring_depth_head(mem), 0);
    assert_false(xrd_vring_closed(mem));
    ((uint8_t *)mem)[16] = 0x40; /* another width */
    assert_false(xrd_vring_valid(mem, xrd_vring_size()));
    free(mem);
}

static void fill(uint8_t *px, uint8_t v)
{
    memset(px, v, XRD_VRING_FRAME_BYTES);
}

static void frames_newest_only_once(void **state)
{
    (void)state;
    void *mem = new_ring();
    static uint8_t got[XRD_VRING_FRAME_BYTES];
    uint64_t seen = 0, t = 0;
    assert_false(xrd_vring_frame_read(mem, &seen, got, &t));
    fill(xrd_vring_frame_begin(mem, 1), 1);
    xrd_vring_frame_commit(mem, 1, 111);
    assert_true(xrd_vring_frame_read(mem, &seen, got, &t));
    assert_int_equal(seen, 1);
    assert_int_equal(t, 111);
    assert_int_equal(got[0], 1);
    assert_false(xrd_vring_frame_read(mem, &seen, got, &t)); /* nothing newer */
    for (uint64_t n = 2; n <= 6; n++) { /* the reader was away: only the newest */
        fill(xrd_vring_frame_begin(mem, n), (uint8_t)n);
        xrd_vring_frame_commit(mem, n, n);
    }
    assert_true(xrd_vring_frame_read(mem, &seen, got, &t));
    assert_int_equal(seen, 6);
    assert_int_equal(got[XRD_VRING_FRAME_BYTES - 1], 6);
    free(mem);
}

static void a_slot_being_written_or_lapped_is_not_read(void **state)
{
    (void)state;
    void *mem = new_ring();
    static uint8_t got[XRD_VRING_FRAME_BYTES];
    uint64_t seen = 0;
    fill(xrd_vring_frame_begin(mem, 1), 1);
    xrd_vring_frame_commit(mem, 1, 0);
    xrd_vring_frame_begin(mem, 5); /* 5 % 4 == 1: the player is writing that slot again */
    assert_false(xrd_vring_frame_read(mem, &seen, got, NULL));
    assert_int_equal(seen, 0);
    xrd_vring_frame_commit(mem, 5, 0);
    assert_true(xrd_vring_frame_read(mem, &seen, got, NULL));
    assert_int_equal(seen, 5);
    /* head ahead of the slot: written over and not yet committed again */
    uint64_t nine = 9;
    memcpy((uint8_t *)mem + XRD_VRING_OFF_FRAME_HEAD, &nine, 8);
    assert_false(xrd_vring_frame_read(mem, &seen, got, NULL));
    free(mem);
}

static void depth_back_with_its_frame(void **state)
{
    (void)state;
    void *mem = new_ring();
    static uint16_t px[XRD_VRING_W * XRD_VRING_H], got[XRD_VRING_W * XRD_VRING_H];
    uint64_t seen = 0, frame = 0;
    assert_false(xrd_vring_depth_read(mem, &seen, got, &frame));
    for (size_t i = 0; i < XRD_VRING_W * XRD_VRING_H; i++)
        px[i] = (uint16_t)i;
    xrd_vring_depth_write(mem, 1, 17, px);
    assert_int_equal(xrd_vring_depth_head(mem), 1);
    assert_true(xrd_vring_depth_read(mem, &seen, got, &frame));
    assert_int_equal(seen, 1);
    assert_int_equal(frame, 17);
    assert_memory_equal(got, px, sizeof px);
    assert_false(xrd_vring_depth_read(mem, &seen, got, &frame));
    xrd_vring_depth_write(mem, 2, 19, px);
    assert_true(xrd_vring_depth_read(mem, &seen, NULL, &frame)); /* NULL: only check */
    assert_int_equal(seen, 2);
    assert_int_equal(frame, 19);
    free(mem);
}

static void close_flag(void **state)
{
    (void)state;
    void *mem = new_ring();
    xrd_vring_close(mem);
    assert_true(xrd_vring_closed(mem));
    assert_int_equal(u32(mem, XRD_VRING_OFF_CLOSED), 1);
    free(mem);
}

static void shm_names_fit_macos(void **state)
{
    (void)state;
    char buf[64];
    assert_int_equal(xrd_vring_shm_name(buf, sizeof buf, 12345, 2), 0);
    assert_string_equal(buf, "/xrdome-12345-2");
    assert_int_equal(xrd_vring_shm_name(buf, sizeof buf, 4194304L, 4294967295u), 0);
    assert_true(strlen(buf) <= 31);
    assert_int_equal(xrd_vring_shm_name(buf, 8, 1, 1), -1);
}

/* Readback is skipped cleanly when 3D is off: no ring, no FBO, nothing read. */
static void frames_go_out_only_for_flat_mono_films_with_3d(void **state)
{
    (void)state;
    assert_true(xrd_framesink_wanted(false, XRD_PROJ_FLAT, XRD_LAYOUT_MONO, true, true));
    assert_false(xrd_framesink_wanted(false, XRD_PROJ_FLAT, XRD_LAYOUT_MONO, false, true));
    assert_false(xrd_framesink_wanted(true, XRD_PROJ_FLAT, XRD_LAYOUT_MONO, true, true));
    assert_false(xrd_framesink_wanted(false, XRD_PROJ_HEQUIRECT, XRD_LAYOUT_MONO, true, true));
    assert_false(xrd_framesink_wanted(false, XRD_PROJ_FLAT, XRD_LAYOUT_SBS_LR, true, true));
    assert_false(xrd_framesink_wanted(false, XRD_PROJ_FLAT, XRD_LAYOUT_TB_LR, true, true));
    assert_false(xrd_framesink_wanted(false, XRD_PROJ_FLAT, XRD_LAYOUT_MONO, true, false));
}

static void shm_ring_with_the_python_helper(void **state)
{
    (void)state;
    char name[32], err[256] = "";
    assert_int_equal(xrd_vring_shm_name(name, sizeof name, (long)getpid(), 77), 0);
    void *mem = xrd_vring_shm_create(name, err, sizeof err);
    assert_non_null(mem);
    assert_true(xrd_vring_valid(mem, xrd_vring_size()));
    for (uint64_t n = 1; n <= 2; n++) {
        uint8_t *px = xrd_vring_frame_begin(mem, n);
        for (int y = 0; y < XRD_VRING_H; y++)
            for (int x = 0; x < XRD_VRING_W; x++) {
                uint8_t *p = px + ((size_t)y * XRD_VRING_W + (size_t)x) * 4;
                p[0] = (uint8_t)x;
                p[1] = (uint8_t)y;
                p[2] = (uint8_t)n;
                p[3] = 255;
            }
        xrd_vring_frame_commit(mem, n, 1234);
    }
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "'%s' -I '%s/vring_peer.py' '%s' '%s' 2", XRD_PYTHON,
             XRD_TESTS_DIR, XRD_TOOLS_DIR, name);
    assert_int_equal(system(cmd), 0);
    /* the helper took the name away; the mapping is still shared */
    assert_true(shm_open(name, O_RDWR, 0600) < 0);
    static uint16_t got[XRD_VRING_W * XRD_VRING_H];
    uint64_t seen = 0, frame = 0;
    assert_true(xrd_vring_depth_read(mem, &seen, got, &frame));
    assert_int_equal(seen, 1);
    assert_int_equal(frame, 2);
    for (size_t i = 0; i < XRD_VRING_W * XRD_VRING_H; i += 997)
        assert_int_equal(got[i], (uint16_t)(i % 65536));
    xrd_vring_shm_destroy(mem, name); /* the name is gone already: fine */
}

static void shm_destroy_removes_the_name(void **state)
{
    (void)state;
    char name[32], err[256] = "";
    assert_int_equal(xrd_vring_shm_name(name, sizeof name, (long)getpid(), 78), 0);
    void *mem = xrd_vring_shm_create(name, err, sizeof err);
    assert_non_null(mem);
    void *again = xrd_vring_shm_create(name, err, sizeof err); /* a stale one is replaced */
    assert_non_null(again);
    munmap(mem, xrd_vring_size());
    xrd_vring_shm_destroy(again, name);
    assert_true(shm_open(name, O_RDWR, 0600) < 0);
    xrd_vring_shm_destroy(NULL, name);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(layout_is_the_documented_one),
        cmocka_unit_test(frames_newest_only_once),
        cmocka_unit_test(a_slot_being_written_or_lapped_is_not_read),
        cmocka_unit_test(depth_back_with_its_frame),
        cmocka_unit_test(close_flag),
        cmocka_unit_test(shm_names_fit_macos),
        cmocka_unit_test(frames_go_out_only_for_flat_mono_films_with_3d),
        cmocka_unit_test(shm_ring_with_the_python_helper),
        cmocka_unit_test(shm_destroy_removes_the_name),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
