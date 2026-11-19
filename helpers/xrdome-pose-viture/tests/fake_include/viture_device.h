/* Stand-in for VITURE's header of the same name, for xrdome's CI only
 * (see viture_glasses_provider.h here). */
#ifndef FAKE_VITURE_DEVICE_H
#define FAKE_VITURE_DEVICE_H

#include "viture_glasses_provider.h"

/* data: roll, pitch, yaw, then the quaternion w, x, y, z. */
typedef void (*VitureImuPoseCallback)(float *data, uint64_t timestamp);

#ifdef __cplusplus
extern "C" {
#endif

VITURE_API int xr_device_provider_register_imu_pose_callback(XRDeviceProviderHandle handle,
                                                             VitureImuPoseCallback imu_pose_callback);
VITURE_API int xr_device_provider_open_imu(XRDeviceProviderHandle handle, uint8_t imu_mode,
                                           uint8_t imu_report_frequency);
VITURE_API int xr_device_provider_close_imu(XRDeviceProviderHandle handle, uint8_t imu_mode);

#ifdef __cplusplus
}
#endif

#endif
