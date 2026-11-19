/* A stand-in for VITURE's libglasses, for the tests: the entry points the
 * helper calls, backed by a fake glasses that reports one fixed pose and keeps
 * a display mode. Each call is appended to the file named by
 * XRD_FAKE_VITURE_LOG, so a test can see the order of the helper's calls.
 *
 *   XRD_FAKE_VITURE_LOG   file the calls are logged to (none: not logged)
 *   XRD_FAKE_VITURE_POSE  "yaw,pitch,roll" in degrees, North-West-Up, the
 *                         pose reported (default 30,-10,5)
 *   XRD_FAKE_VITURE_MODE  display mode the glasses start in (default 0x31)
 *   XRD_FAKE_VITURE_HZ    samples per second (default 500)
 *
 * Product ids 0x1019 and 0x101D are the Pro (Gen 1); 0x7001 is a Carina
 * device; no other id is valid. Built with -DFAKE_OMIT_START it lacks
 * xr_device_provider_start. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "viture_glasses_provider.h"
#include "viture_device.h"
#include "viture_protocol_public.h"

typedef struct {
    int pid;
    int mode;
    VitureImuPoseCallback cb;
    pthread_t thread;
    int thread_started;
    atomic_int run;
    float pose[7];
    int period_ms;
} fake_glasses;

static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void note(const char *fmt, ...)
{
    const char *path = getenv("XRD_FAKE_VITURE_LOG");
    if (!path || !path[0])
        return;
    FILE *f = fopen(path, "ab");
    if (!f)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static int pid_ok(int pid)
{
    return pid == 0x1019 || pid == 0x101D || pid == 0x7001;
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

/* q = Rz(yaw) * Ry(pitch) * Rx(roll), written out as the product of three
 * half-angle quaternions. */
static void build_pose(float out[7], double yaw_d, double pitch_d, double roll_d)
{
    const double d2r = 3.14159265358979323846 / 180.0;
    double cy = cos(yaw_d * d2r / 2), sy = sin(yaw_d * d2r / 2);
    double cp = cos(pitch_d * d2r / 2), sp = sin(pitch_d * d2r / 2);
    double cr = cos(roll_d * d2r / 2), sr = sin(roll_d * d2r / 2);
    /* Euler fields in radians, in the callback's order: roll, pitch, yaw. */
    out[0] = (float)(roll_d * d2r);
    out[1] = (float)(pitch_d * d2r);
    out[2] = (float)(yaw_d * d2r);
    out[3] = (float)(cr * cp * cy + sr * sp * sy);
    out[4] = (float)(sr * cp * cy - cr * sp * sy);
    out[5] = (float)(cr * sp * cy + sr * cp * sy);
    out[6] = (float)(cr * cp * sy - sr * sp * cy);
}

VITURE_API XRDeviceProviderHandle xr_device_provider_create(int product_id)
{
    note("create %d", product_id);
    if (!pid_ok(product_id))
        return NULL;
    fake_glasses *g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    g->pid = product_id;
    const char *m = getenv("XRD_FAKE_VITURE_MODE");
    g->mode = m && m[0] ? (int)strtol(m, NULL, 0) : VITURE_DISPLAY_MODE_1920_1080_60HZ;
    double yaw = 30, pitch = -10, roll = 5;
    const char *p = getenv("XRD_FAKE_VITURE_POSE");
    if (p && p[0])
        sscanf(p, "%lf,%lf,%lf", &yaw, &pitch, &roll);
    build_pose(g->pose, yaw, pitch, roll);
    int hz = 500;
    const char *h = getenv("XRD_FAKE_VITURE_HZ");
    if (h && h[0])
        hz = atoi(h);
    g->period_ms = hz > 0 && hz < 1000 ? 1000 / hz : 1;
    atomic_init(&g->run, 0);
    return g;
}

VITURE_API int xr_device_provider_initialize(XRDeviceProviderHandle handle, const char *custom_config,
                                             const char *cache_file_dir)
{
    (void)custom_config;
    (void)cache_file_dir;
    note("initialize");
    return handle ? VITURE_GLASSES_SUCCESS : VITURE_GLASSES_ERROR_INVALID_PARAM;
}

#ifndef FAKE_OMIT_START
VITURE_API int xr_device_provider_start(XRDeviceProviderHandle handle)
{
    note("start");
    return handle ? VITURE_GLASSES_SUCCESS : VITURE_GLASSES_ERROR_INVALID_PARAM;
}
#endif

VITURE_API int xr_device_provider_stop(XRDeviceProviderHandle handle)
{
    note("stop");
    return handle ? VITURE_GLASSES_SUCCESS : VITURE_GLASSES_ERROR_INVALID_PARAM;
}

VITURE_API int xr_device_provider_shutdown(XRDeviceProviderHandle handle)
{
    note("shutdown");
    return handle ? VITURE_GLASSES_SUCCESS : VITURE_GLASSES_ERROR_INVALID_PARAM;
}

VITURE_API void xr_device_provider_destroy(XRDeviceProviderHandle handle)
{
    note("destroy");
    free(handle);
}

VITURE_API int xr_device_provider_get_device_type(XRDeviceProviderHandle handle)
{
    fake_glasses *g = handle;
    if (!g)
        return VITURE_GLASSES_ERROR_INVALID_PARAM;
    return g->pid == 0x7001 ? XR_DEVICE_TYPE_VITURE_CARINA : XR_DEVICE_TYPE_VITURE_GEN1;
}

VITURE_API int xr_device_provider_is_product_id_valid(int product_id)
{
    return pid_ok(product_id);
}

VITURE_API int xr_device_provider_is_product_support_imu_frequency(int product_id, int imu_mode,
                                                                   int imu_report_frequency)
{
    return pid_ok(product_id) && imu_mode == VITURE_IMU_MODE_POSE && imu_report_frequency >= 0 &&
           imu_report_frequency <= VITURE_IMU_FREQUENCY_HIGH;
}

VITURE_API int xr_device_provider_register_imu_pose_callback(XRDeviceProviderHandle handle,
                                                             VitureImuPoseCallback imu_pose_callback)
{
    fake_glasses *g = handle;
    note("register_imu_pose %s", imu_pose_callback ? "callback" : "null");
    if (!g || !imu_pose_callback)
        return VITURE_GLASSES_ERROR_INVALID_PARAM;
    g->cb = imu_pose_callback;
    return VITURE_GLASSES_SUCCESS;
}

static void *imu_thread(void *arg)
{
    fake_glasses *g = arg;
    uint64_t ts = 0;
    while (atomic_load(&g->run)) {
        float data[7];
        memcpy(data, g->pose, sizeof data);
        g->cb(data, ts++);
        sleep_ms(g->period_ms);
    }
    return NULL;
}

VITURE_API int xr_device_provider_open_imu(XRDeviceProviderHandle handle, uint8_t imu_mode,
                                           uint8_t imu_report_frequency)
{
    fake_glasses *g = handle;
    note("open_imu %d %d", imu_mode, imu_report_frequency);
    if (!g || !g->cb || g->thread_started)
        return VITURE_GLASSES_ERROR_INVALID_PARAM;
    atomic_store(&g->run, 1);
    if (pthread_create(&g->thread, NULL, imu_thread, g) != 0)
        return VITURE_GLASSES_ERROR_UNKNOWN;
    g->thread_started = 1;
    return VITURE_GLASSES_SUCCESS;
}

VITURE_API int xr_device_provider_close_imu(XRDeviceProviderHandle handle, uint8_t imu_mode)
{
    fake_glasses *g = handle;
    note("close_imu %d", imu_mode);
    if (!g)
        return VITURE_GLASSES_ERROR_INVALID_PARAM;
    if (g->thread_started) {
        atomic_store(&g->run, 0);
        pthread_join(g->thread, NULL);
        g->thread_started = 0;
    }
    return VITURE_GLASSES_SUCCESS;
}

VITURE_API int xr_device_provider_get_display_mode(XRDeviceProviderHandle handle)
{
    fake_glasses *g = handle;
    note("get_display_mode");
    return g ? g->mode : VITURE_GLASSES_ERROR_INVALID_PARAM;
}

VITURE_API int xr_device_provider_set_display_mode(XRDeviceProviderHandle handle, int display_mode)
{
    fake_glasses *g = handle;
    note("set_display_mode %d", display_mode);
    if (!g)
        return VITURE_GLASSES_ERROR_INVALID_PARAM;
    g->mode = display_mode;
    return VITURE_GLASSES_SUCCESS;
}
