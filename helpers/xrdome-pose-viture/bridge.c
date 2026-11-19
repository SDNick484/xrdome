#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* clock_gettime, nanosleep */
#endif

#include "bridge.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "convert.h"
#include "packet.h"
#include "udp_out.h"

static const double FREQ_HZ[VITURE_FREQ_COUNT] = {60, 90, 120, 240, 500, 1000};

int viture_pick_frequency(double rate_hz, unsigned mask)
{
    int best = -1;
    for (int i = 0; i < VITURE_FREQ_COUNT; i++) {
        if (!(mask & (1u << i)))
            continue;
        best = i; /* ends as the highest */
        if (FREQ_HZ[i] >= 2.0 * rate_hz)
            return i;
    }
    return best;
}

bool viture_sbs_target(int current, int *target)
{
    if (current == VITURE_DISPLAY_MODE_3840_1080_60HZ || current == VITURE_DISPLAY_MODE_3840_1080_90HZ)
        return false;
    *target = VITURE_DISPLAY_MODE_3840_1080_60HZ;
    return true;
}

static uint64_t now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = {0, (long)ms * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

/* The SDK's pose callback has no user pointer, so the session it feeds is a
 * global: one helper process, one session. The callback runs on the SDK's own
 * thread; the main thread only reads the counters. */
typedef struct {
    viture_udp udp;
    double interval_ns;
    long limit;
    atomic_long received; /* poses the SDK delivered */
    atomic_long sent;     /* packets handed to the system */
    atomic_long rejected; /* poses that could not be converted */
    atomic_int done;      /* the packet limit was reached */
    uint64_t next_ns;     /* callback thread only */
    uint32_t frame;       /* callback thread only */
} session;

static session *g_session;

static void on_pose(float *data, uint64_t timestamp)
{
    (void)timestamp;
    session *s = g_session;
    if (!s || !data)
        return;
    atomic_fetch_add(&s->received, 1);
    if (atomic_load(&s->done))
        return;

    /* Rate cap: a schedule, not a gap since the last packet, so a stream at
     * 500 Hz capped at 250 Hz gives 250 and not 167. A late sample resets it. */
    uint64_t t = now_ns();
    if (t < s->next_ns)
        return;
    s->next_ns += (uint64_t)s->interval_ns;
    if (s->next_ns < t)
        s->next_ns = t + (uint64_t)s->interval_ns;

    double ypr[3];
    if (viture_pose_to_ypr(data, ypr) != 0) {
        atomic_fetch_add(&s->rejected, 1);
        return;
    }
    uint8_t pkt[VITURE_OT_PACKET_SIZE];
    viture_ot_encode(ypr, s->frame++, pkt);
    if (viture_udp_send(&s->udp, pkt, sizeof pkt) == 0) {
        long n = atomic_fetch_add(&s->sent, 1) + 1;
        if (s->limit > 0 && n >= s->limit)
            atomic_store(&s->done, 1);
    }
}

#define SILENT_WARN_NS 3000000000ull

int viture_bridge_run(const viture_options *o, const viture_api *api, int pid, atomic_int *stop,
                      FILE *out, long *sent)
{
    int rc = 1;
    if (sent)
        *sent = 0;

    session s;
    memset(&s, 0, sizeof s);
    s.interval_ns = 1e9 / o->rate;
    s.limit = o->count;
    atomic_init(&s.received, 0);
    atomic_init(&s.sent, 0);
    atomic_init(&s.rejected, 0);
    atomic_init(&s.done, 0);

    char err[200];
    if (viture_udp_open(&s.udp, o->host, o->port, err, sizeof err) != 0) {
        fprintf(out, "xrdome-pose-viture: %s\n", err);
        return 1;
    }

    XRDeviceProviderHandle h = api->create(pid);
    if (!h) {
        fprintf(out,
                "xrdome-pose-viture: the SDK could not open the glasses (product id 0x%04X): "
                "are they plugged in, is the udev rule installed, and is XRLinuxDriver or "
                "another program holding them?\n",
                pid);
        viture_udp_close(&s.udp);
        return 1;
    }

    bool initialized = false, started = false, imu_open = false, mode_changed = false;
    int saved_mode = 0;

    int dev = api->get_device_type(h);
    if (dev == XR_DEVICE_TYPE_VITURE_CARINA) {
        fprintf(out,
                "xrdome-pose-viture: these glasses (Carina, Luma Ultra) report their pose through a "
                "different SDK call, which this helper does not use\n");
        goto cleanup;
    }

    int r = api->initialize(h, NULL, NULL);
    if (r != VITURE_GLASSES_SUCCESS) {
        fprintf(out, "xrdome-pose-viture: xr_device_provider_initialize failed (%d)\n", r);
        goto cleanup;
    }
    initialized = true;
    r = api->start(h);
    if (r != VITURE_GLASSES_SUCCESS) {
        fprintf(out, "xrdome-pose-viture: xr_device_provider_start failed (%d)\n", r);
        goto cleanup;
    }
    started = true;

    if (o->sbs) {
        int cur = api->get_display_mode(h);
        int target;
        if (cur < 0) {
            fprintf(out, "xrdome-pose-viture: --sbs: cannot read the display mode (%d); not switching\n",
                    cur);
        } else if (!viture_sbs_target(cur, &target)) {
            fprintf(out, "xrdome-pose-viture: --sbs: the glasses are already in a 3840x1080 mode (0x%02X)\n",
                    cur);
        } else {
            r = api->set_display_mode(h, target);
            if (r == VITURE_GLASSES_SUCCESS) {
                saved_mode = cur;
                mode_changed = true;
                fprintf(out, "xrdome-pose-viture: display mode 0x%02X -> 0x%02X (3840x1080 SBS)\n", cur,
                        target);
            } else {
                fprintf(out, "xrdome-pose-viture: --sbs: set_display_mode(0x%02X) failed (%d); not switching\n",
                        target, r);
            }
        }
    }

    unsigned mask = 0;
    for (int i = 0; i < VITURE_FREQ_COUNT; i++)
        if (api->is_product_support_imu_frequency(pid, VITURE_IMU_MODE_POSE, i) == 1)
            mask |= 1u << i;
    int freq = viture_pick_frequency(o->rate, mask);
    if (freq < 0) {
        fprintf(out, "xrdome-pose-viture: the SDK reports no pose frequency for product id 0x%04X\n", pid);
        goto cleanup;
    }

    g_session = &s;
    r = api->register_imu_pose_callback(h, on_pose);
    if (r != VITURE_GLASSES_SUCCESS) {
        fprintf(out, "xrdome-pose-viture: register_imu_pose_callback failed (%d)\n", r);
        goto cleanup;
    }
    r = api->open_imu(h, VITURE_IMU_MODE_POSE, (uint8_t)freq);
    if (r != VITURE_GLASSES_SUCCESS) {
        fprintf(out, "xrdome-pose-viture: open_imu failed (%d)\n", r);
        goto cleanup;
    }
    imu_open = true;
    fprintf(out, "xrdome-pose-viture: sending pose to %s:%d, at most %g packets/s (Ctrl+C to stop)\n", o->host,
            o->port, o->rate);

    uint64_t t0 = now_ns();
    long last_received = 0;
    uint64_t last_change = t0;
    bool warned = false;
    while (!atomic_load(stop) && !atomic_load(&s.done)) {
        sleep_ms(20);
        long got = atomic_load(&s.received);
        uint64_t t = now_ns();
        if (got != last_received) {
            if (warned)
                fprintf(out, "xrdome-pose-viture: pose samples are arriving again\n");
            last_received = got;
            last_change = t;
            warned = false;
        } else if (!warned && t - last_change > SILENT_WARN_NS) {
            fputs(got == 0 ? "xrdome-pose-viture: no pose samples after 3 s: is anything else "
                             "holding the glasses?\n"
                           : "xrdome-pose-viture: no pose samples for 3 s\n",
                  out);
            warned = true;
        }
    }
    rc = 0;

cleanup:
    /* Close the IMU before stop(), as the SDK's documentation says; the
     * display mode goes back while the provider still runs. */
    if (imu_open)
        api->close_imu(h, VITURE_IMU_MODE_POSE);
    g_session = NULL;
    if (mode_changed) {
        r = api->set_display_mode(h, saved_mode);
        if (r == VITURE_GLASSES_SUCCESS)
            fprintf(out, "xrdome-pose-viture: display mode back to 0x%02X\n", saved_mode);
        else
            fprintf(out, "xrdome-pose-viture: could not restore display mode 0x%02X (%d): use the glasses' button\n",
                    saved_mode, r);
    }
    if (started)
        api->stop(h);
    if (initialized)
        api->shutdown(h);
    api->destroy(h);
    viture_udp_close(&s.udp);
    if (sent)
        *sent = atomic_load(&s.sent);
    if (rc == 0 && atomic_load(&s.rejected) > 0)
        fprintf(out, "xrdome-pose-viture: %ld poses skipped (not a usable quaternion)\n",
                atomic_load(&s.rejected));
    return rc;
}
