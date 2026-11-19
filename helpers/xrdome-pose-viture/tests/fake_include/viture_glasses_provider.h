/* Stand-in for VITURE's header of the same name, for xrdome's CI only.
 * Written for the tests from the calls the helper makes; it declares nothing
 * else and is not VITURE's text. Build against VITURE's own headers with
 * -Dviture_sdk_include=PATH. */
#ifndef FAKE_VITURE_GLASSES_PROVIDER_H
#define FAKE_VITURE_GLASSES_PROVIDER_H

#include <stdint.h>

#ifndef VITURE_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define VITURE_API __declspec(dllexport)
#  else
#    define VITURE_API __attribute__((visibility("default")))
#  endif
#endif

#define VITURE_GLASSES_SUCCESS 0
#define VITURE_GLASSES_ERROR_INVALID_PARAM (-1)
#define VITURE_GLASSES_ERROR_NOT_SUPPORTED (-4)
#define VITURE_GLASSES_ERROR_UNKNOWN (-99)

typedef void *XRDeviceProviderHandle;

typedef enum {
    XR_DEVICE_TYPE_VITURE_GEN1 = 0,
    XR_DEVICE_TYPE_VITURE_GEN2 = 1,
    XR_DEVICE_TYPE_VITURE_CARINA = 2
} XRDeviceType;

#ifdef __cplusplus
extern "C" {
#endif

VITURE_API XRDeviceProviderHandle xr_device_provider_create(int product_id);
VITURE_API int xr_device_provider_initialize(XRDeviceProviderHandle handle, const char *custom_config,
                                             const char *cache_file_dir);
VITURE_API int xr_device_provider_start(XRDeviceProviderHandle handle);
VITURE_API int xr_device_provider_stop(XRDeviceProviderHandle handle);
VITURE_API int xr_device_provider_shutdown(XRDeviceProviderHandle handle);
VITURE_API void xr_device_provider_destroy(XRDeviceProviderHandle handle);
VITURE_API int xr_device_provider_get_device_type(XRDeviceProviderHandle handle);
VITURE_API int xr_device_provider_is_product_id_valid(int product_id);
VITURE_API int xr_device_provider_is_product_support_imu_frequency(int product_id, int imu_mode,
                                                                   int imu_report_frequency);

#ifdef __cplusplus
}
#endif

#endif
