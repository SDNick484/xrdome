#ifndef XRD_POSE_USB_IO_H
#define XRD_POSE_USB_IO_H

#include <stddef.h>
#include <stdint.h>

/* One endpoint pair of a USB device, as the pose backends see it: usbfs on
 * Linux and Android (pose/usbfs.h), a fake in the tests.
 *
 * write: sends len bytes; returns the bytes moved or -1.
 * read:  receives up to len bytes; returns the bytes moved, 0 on timeout or
 *        -1 on an error.
 * Either may be NULL for a pair without that direction. */
typedef struct {
    int (*write)(void *ctx, const uint8_t *buf, size_t len, int timeout_ms);
    int (*read)(void *ctx, uint8_t *buf, size_t len, int timeout_ms);
    void *ctx;
} xrd_usb_io;

#endif
