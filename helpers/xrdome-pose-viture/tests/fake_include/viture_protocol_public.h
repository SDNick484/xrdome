/* Stand-in for VITURE's header of the same name, for xrdome's CI only
 * (see viture_glasses_provider.h here). */
#ifndef FAKE_VITURE_PROTOCOL_PUBLIC_H
#define FAKE_VITURE_PROTOCOL_PUBLIC_H

#include "viture_glasses_provider.h"

#define VITURE_DISPLAY_MODE_1920_1080_60HZ 0x31
#define VITURE_DISPLAY_MODE_3840_1080_60HZ 0x32
#define VITURE_DISPLAY_MODE_1920_1080_90HZ 0x33
#define VITURE_DISPLAY_MODE_1920_1080_120HZ 0x34
#define VITURE_DISPLAY_MODE_3840_1080_90HZ 0x35

#define VITURE_IMU_MODE_RAW 0
#define VITURE_IMU_MODE_POSE 1

#define VITURE_IMU_FREQUENCY_LOW 0
#define VITURE_IMU_FREQUENCY_MEDIUM_LOW 1
#define VITURE_IMU_FREQUENCY_MEDIUM 2
#define VITURE_IMU_FREQUENCY_MEDIUM_HIGH 3
#define VITURE_IMU_FREQUENCY_HIGH 4
#define VITURE_IMU_FREQUENCY_ULTRA_HIGH 5

#ifdef __cplusplus
extern "C" {
#endif

VITURE_API int xr_device_provider_get_display_mode(XRDeviceProviderHandle handle);
VITURE_API int xr_device_provider_set_display_mode(XRDeviceProviderHandle handle, int display_mode);

#ifdef __cplusplus
}
#endif

#endif
