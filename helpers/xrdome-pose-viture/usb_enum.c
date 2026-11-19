#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* opendir */
#endif

#include "usb_enum.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <setupapi.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#else
#include <dirent.h>
#endif

/* Adds pid to pids unless it is there or the list is full. */
static int add_pid(int *pids, int n, int max, int pid)
{
    for (int i = 0; i < n; i++)
        if (pids[i] == pid)
            return n;
    if (n < max)
        pids[n++] = pid;
    return n;
}

/* "VID_35CA" style field: four hex digits after the tag. */
static int hex4(const char *s, unsigned *out)
{
    for (int i = 0; i < 4; i++)
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    char tmp[5];
    memcpy(tmp, s, 4);
    tmp[4] = '\0';
    *out = (unsigned)strtoul(tmp, NULL, 16);
    return 1;
}

int viture_parse_hwid(const char *hwid, unsigned *vid, unsigned *pid)
{
    const char *v = strstr(hwid, "VID_");
    const char *p = strstr(hwid, "PID_");
    unsigned a, b;
    if (!v || !p || !hex4(v + 4, &a) || !hex4(p + 4, &b))
        return 0;
    *vid = a;
    *pid = b;
    return 1;
}

#if defined(_WIN32)

int viture_usb_find(int *pids, int max)
{
    int n = 0;
    HDEVINFO info = SetupDiGetClassDevsA(NULL, "USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (info == INVALID_HANDLE_VALUE)
        return 0;
    SP_DEVINFO_DATA dd;
    dd.cbSize = sizeof dd;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(info, i, &dd); i++) {
        char buf[2048];
        DWORD type = 0;
        memset(buf, 0, sizeof buf);
        /* REG_MULTI_SZ: strings one after another, an empty one last. The last
         * two bytes stay zero (the call gets one byte less). */
        if (!SetupDiGetDeviceRegistryPropertyA(info, &dd, SPDRP_HARDWAREID, &type, (PBYTE)buf,
                                               sizeof buf - 2, NULL))
            continue;
        for (const char *p = buf; *p; p += strlen(p) + 1) {
            unsigned vid, pid;
            if (viture_parse_hwid(p, &vid, &pid) && vid == VITURE_USB_VENDOR)
                n = add_pid(pids, n, max, (int)pid);
        }
    }
    SetupDiDestroyDeviceInfoList(info);
    return n;
}

#elif defined(__APPLE__)

/* A 16-bit number property of a registry entry. */
static int number_property(io_service_t svc, const char *key, unsigned *out)
{
    CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    if (!k)
        return 0;
    CFTypeRef p = IORegistryEntryCreateCFProperty(svc, k, kCFAllocatorDefault, 0);
    CFRelease(k);
    if (!p)
        return 0;
    int ok = 0;
    int v = 0;
    if (CFGetTypeID(p) == CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)p, kCFNumberIntType, &v)) {
        *out = (unsigned)v;
        ok = 1;
    }
    CFRelease(p);
    return ok;
}

int viture_usb_find(int *pids, int max)
{
    int n = 0;
    CFMutableDictionaryRef match = IOServiceMatching("IOUSBHostDevice");
    io_iterator_t it = 0;
    /* Port 0: the default main port. The call consumes match. */
    if (!match || IOServiceGetMatchingServices(0, match, &it) != KERN_SUCCESS)
        return 0;
    io_service_t svc;
    while ((svc = IOIteratorNext(it)) != 0) {
        unsigned vid = 0, pid = 0;
        if (number_property(svc, "idVendor", &vid) && vid == VITURE_USB_VENDOR &&
            number_property(svc, "idProduct", &pid))
            n = add_pid(pids, n, max, (int)pid);
        IOObjectRelease(svc);
    }
    IOObjectRelease(it);
    return n;
}

#else

/* A sysfs attribute: hex text, "35ca\n". */
static int read_hex(const char *path, unsigned *out)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char buf[32];
    char *got = fgets(buf, sizeof buf, f);
    fclose(f);
    if (!got)
        return 0;
    char *end;
    unsigned long v = strtoul(buf, &end, 16);
    if (end == buf)
        return 0;
    *out = (unsigned)v;
    return 1;
}

int viture_usb_scan_sysfs(const char *root, int *pids, int max)
{
    DIR *d = opendir(root);
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        /* ".", "..", and interfaces ("1-2:1.0"); devices are "1-2", "usb1". */
        if (e->d_name[0] == '.' || strchr(e->d_name, ':'))
            continue;
        char path[512];
        unsigned vid, pid;
        snprintf(path, sizeof path, "%s/%s/idVendor", root, e->d_name);
        if (!read_hex(path, &vid) || vid != VITURE_USB_VENDOR)
            continue;
        snprintf(path, sizeof path, "%s/%s/idProduct", root, e->d_name);
        if (read_hex(path, &pid))
            n = add_pid(pids, n, max, (int)pid);
    }
    closedir(d);
    return n;
}

int viture_usb_find(int *pids, int max)
{
    return viture_usb_scan_sysfs("/sys/bus/usb/devices", pids, max);
}

#endif

#if defined(_WIN32) || defined(__APPLE__)
/* Only Linux has a sysfs to scan. */
int viture_usb_scan_sysfs(const char *root, int *pids, int max)
{
    (void)root;
    (void)pids;
    (void)max;
    return 0;
}
#endif
