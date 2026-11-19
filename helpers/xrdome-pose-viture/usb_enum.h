#ifndef XRD_VITURE_USB_ENUM_H
#define XRD_VITURE_USB_ENUM_H

#include <stddef.h>

#define VITURE_USB_VENDOR 0x35CA

/* USB product ids of the attached devices whose vendor id is VITURE's, at
 * most max, none twice. The caller keeps those the SDK accepts
 * (xr_device_provider_is_product_id_valid). Returns the count; 0 also when
 * the platform cannot be asked. */
int viture_usb_find(int *pids, int max);

/* Parses a Windows hardware id such as "USB\VID_35CA&PID_101D&MI_00": true and
 * vid and pid set when both fields are there. Pure; used on Windows. */
int viture_parse_hwid(const char *hwid, unsigned *vid, unsigned *pid);

/* Linux: the same scan over a sysfs USB tree (root is /sys/bus/usb/devices). */
int viture_usb_scan_sysfs(const char *root, int *pids, int max);

#endif
