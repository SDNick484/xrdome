#ifndef XRD_VITURE_LOADER_H
#define XRD_VITURE_LOADER_H

/* VITURE's SDK is loaded at run time (dlopen, LoadLibraryW): the helper has
 * no link-time dependency on it. The SDK's headers supply the types and
 * constants; this file declares each entry point it calls with the exact
 * signature and checks it against the header at compile time. */

#include <stddef.h>
#include <stdint.h> /* viture_device.h uses uint8_t without including it */

#include "viture_glasses_provider.h"
#include "viture_device.h"
#include "viture_protocol_public.h"

typedef struct {
    void *lib; /* the OS's library handle */
    XRDeviceProviderHandle (*create)(int product_id);
    int (*initialize)(XRDeviceProviderHandle, const char *custom_config, const char *cache_dir);
    int (*start)(XRDeviceProviderHandle);
    int (*stop)(XRDeviceProviderHandle);
    int (*shutdown)(XRDeviceProviderHandle);
    void (*destroy)(XRDeviceProviderHandle);
    int (*get_device_type)(XRDeviceProviderHandle);
    int (*is_product_id_valid)(int product_id);
    int (*is_product_support_imu_frequency)(int product_id, int imu_mode, int imu_report_frequency);
    int (*register_imu_pose_callback)(XRDeviceProviderHandle, VitureImuPoseCallback);
    int (*open_imu)(XRDeviceProviderHandle, uint8_t imu_mode, uint8_t imu_report_frequency);
    int (*close_imu)(XRDeviceProviderHandle, uint8_t imu_mode);
    int (*get_display_mode)(XRDeviceProviderHandle);
    int (*set_display_mode)(XRDeviceProviderHandle, int display_mode);
} viture_api;

/* The SDK library's file name on this platform: libglasses.so,
 * libglasses.dylib or libglasses.dll. */
const char *viture_default_lib(void);

/* Loads path (NULL: viture_default_lib()) and resolves every entry point.
 * Returns 0, or -1 with a message in err naming the library, or the missing
 * symbol; api is then unusable and nothing stays loaded. */
int viture_load(const char *path, viture_api *api, char *err, size_t errlen);

/* Unloads the library. The pointers in api must not be called afterwards. */
void viture_unload(viture_api *api);

#endif
