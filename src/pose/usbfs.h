#ifndef XRD_POSE_USBFS_H
#define XRD_POSE_USBFS_H

/* USB transfers through usbfs (/dev/bus/usb/BBB/DDD), Linux and Android
 * only: on Android the app opens the device through UsbManager and passes
 * its file descriptor down; on Linux the backend opens the node itself.
 * Shared by the glasses' pose backends. Not compiled elsewhere. */

#ifdef __linux__

#include <stdint.h>

#include "pose/usb_io.h"

/* Opens the device node read-write; -1 with errno set on failure. */
int xrd_usbfs_open(const char *dev_path);

/* Takes interface ifno from the kernel driver bound to it (USBDEVFS_DISCONNECT,
 * skipped when usbfs itself holds it) and claims it for this fd
 * (USBDEVFS_CLAIMINTERFACE). An interface this fd claimed already is a
 * success (the kernel returns 0 for it: the Android case, where the app
 * claimed it on the same descriptor); EBUSY means another holder and fails.
 * 0 or -1 with errno set. */
int xrd_usbfs_claim(int fd, int ifno);

/* Releases interface ifno and gives it back to the kernel driver
 * (USBDEVFS_CONNECT). Errors are ignored. */
void xrd_usbfs_release(int fd, int ifno);

/* Fills *out with an endpoint pair on fd: reads from ep_in, writes to
 * ep_out (0 for a read-only pair: write then fails), each one
 * USBDEVFS_BULK, which the kernel turns into an interrupt transfer on an
 * interrupt endpoint. Holds no memory: nothing to free; the pair is valid
 * while fd is open. 0, or -1 for a bad fd or endpoint. */
int xrd_usbfs_io(int fd, uint8_t ep_in, uint8_t ep_out, xrd_usb_io *out);

#endif

#endif
