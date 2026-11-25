/* USB transfers through usbfs, Linux and Android only (see usbfs.h). */

#ifdef __linux__

#define _POSIX_C_SOURCE 200809L /* open, O_CLOEXEC */

#include "pose/usbfs.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <string.h>
#include <sys/ioctl.h>

int xrd_usbfs_open(const char *dev_path)
{
    if (!dev_path || !*dev_path) {
        errno = EINVAL;
        return -1;
    }
    return open(dev_path, O_RDWR | O_CLOEXEC);
}

int xrd_usbfs_claim(int fd, int ifno)
{
    if (fd < 0 || ifno < 0) {
        errno = EINVAL;
        return -1;
    }
    /* USBDEVFS_DISCONNECT unbinds whatever driver holds the interface,
     * usbfs included, so it would drop a claim this very descriptor holds
     * (the Android app's). Ask first. */
    struct usbdevfs_getdriver gd;
    memset(&gd, 0, sizeof gd);
    gd.interface = (unsigned int)ifno;
    if (ioctl(fd, USBDEVFS_GETDRIVER, &gd) == 0 && strcmp(gd.driver, "usbfs") != 0) {
        struct usbdevfs_ioctl cmd;
        memset(&cmd, 0, sizeof cmd);
        cmd.ifno = ifno;
        cmd.ioctl_code = USBDEVFS_DISCONNECT;
        cmd.data = NULL;
        if (ioctl(fd, USBDEVFS_IOCTL, &cmd) < 0 && errno != ENODATA)
            return -1;
    }
    unsigned int n = (unsigned int)ifno;
    return ioctl(fd, USBDEVFS_CLAIMINTERFACE, &n) < 0 ? -1 : 0;
}

void xrd_usbfs_release(int fd, int ifno)
{
    if (fd < 0 || ifno < 0)
        return;
    int saved = errno;
    unsigned int n = (unsigned int)ifno;
    (void)ioctl(fd, USBDEVFS_RELEASEINTERFACE, &n);
    struct usbdevfs_ioctl cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.ifno = ifno;
    cmd.ioctl_code = USBDEVFS_CONNECT;
    cmd.data = NULL;
    (void)ioctl(fd, USBDEVFS_IOCTL, &cmd);
    errno = saved;
}

/* The pair lives in the ctx pointer itself: fd << 16 | ep_in << 8 | ep_out. */
static void unpack(void *ctx, int *fd, unsigned int *ep_in, unsigned int *ep_out)
{
    uintptr_t v = (uintptr_t)ctx;
    *fd = (int)(v >> 16);
    *ep_in = (unsigned int)((v >> 8) & 0xFFu);
    *ep_out = (unsigned int)(v & 0xFFu);
}

static int bulk(int fd, unsigned int ep, uint8_t *buf, size_t len, int timeout_ms)
{
    if (len > 0xFFFFu) {
        errno = EINVAL;
        return -1;
    }
    struct usbdevfs_bulktransfer bt;
    memset(&bt, 0, sizeof bt);
    bt.ep = ep;
    bt.len = (unsigned int)len;
    bt.timeout = timeout_ms > 0 ? (unsigned int)timeout_ms : 1u; /* 0 would wait forever */
    bt.data = buf;
    int n = ioctl(fd, USBDEVFS_BULK, &bt);
    if (n < 0)
        return errno == ETIMEDOUT ? 0 : -1;
    return n;
}

static int usbfs_read(void *ctx, uint8_t *buf, size_t len, int timeout_ms)
{
    int fd;
    unsigned int ep_in, ep_out;
    unpack(ctx, &fd, &ep_in, &ep_out);
    if (ep_in == 0) {
        errno = EINVAL;
        return -1;
    }
    return bulk(fd, ep_in, buf, len, timeout_ms);
}

static int usbfs_write(void *ctx, const uint8_t *buf, size_t len, int timeout_ms)
{
    int fd;
    unsigned int ep_in, ep_out;
    unpack(ctx, &fd, &ep_in, &ep_out);
    if (ep_out == 0) {
        errno = EINVAL;
        return -1;
    }
    /* usbfs takes a non-const buffer for both directions; it only reads it
     * for an OUT endpoint. */
    uint8_t tmp[512];
    if (len > sizeof tmp) {
        errno = EINVAL;
        return -1;
    }
    memcpy(tmp, buf, len);
    int n = bulk(fd, ep_out, tmp, len, timeout_ms);
    if (n == 0 && len > 0) { /* a write that timed out moved nothing: an error */
        errno = ETIMEDOUT;
        return -1;
    }
    return n;
}

int xrd_usbfs_io(int fd, uint8_t ep_in, uint8_t ep_out, xrd_usb_io *out)
{
    if (!out || fd < 0 || (uintptr_t)fd > (UINTPTR_MAX >> 16) || (ep_in != 0 && !(ep_in & 0x80u)) ||
        (ep_out & 0x80u)) {
        errno = EINVAL;
        return -1;
    }
    out->ctx = (void *)(((uintptr_t)fd << 16) | ((uintptr_t)ep_in << 8) | (uintptr_t)ep_out);
    out->read = usbfs_read;
    out->write = usbfs_write;
    return 0;
}

#else

/* Not Linux: nothing here (ISO C wants a declaration in every file). */
typedef int xrd_usbfs_unused;

#endif
