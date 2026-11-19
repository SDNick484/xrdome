#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bridge.h"
#include "options.h"
#include "usb_enum.h"
#include "viture_loader.h"
#include "xrdriver_check.h"

#ifndef XRD_VERSION
#define XRD_VERSION "0"
#endif

/* Ctrl+C and termination only set a flag; the session ends its own way, which
 * puts the display mode back. (Windows runs the handler on its own thread.) */
static atomic_int g_stop;

static void on_signal(int sig)
{
    (void)sig;
    atomic_store(&g_stop, 1);
}

int main(int argc, char **argv)
{
    viture_options o;
    char err[300];
    if (viture_parse_args(argc, argv, &o, err, sizeof err) != 0) {
        fprintf(stderr, "xrdome-pose-viture: %s (see --help)\n", err);
        return 2;
    }
    if (o.help) {
        fputs(viture_usage(), stdout);
        return 0;
    }
    if (o.version) {
        puts("xrdome-pose-viture " XRD_VERSION);
        return 0;
    }

#ifdef __linux__
    if (viture_xrdriver_running(viture_xrdriver_dir(), (long long)time(NULL))) {
        fprintf(stderr,
                "xrdome-pose-viture: XRLinuxDriver is running (a fresh heartbeat in %s/xr_driver_state). "
                "It and VITURE's SDK cannot both hold the glasses: stop xrDriver first.\n",
                viture_xrdriver_dir());
        return 1;
    }
#endif

    viture_api api;
    if (viture_load(o.lib, &api, err, sizeof err) != 0) {
        fprintf(stderr, "xrdome-pose-viture: %s\n", err);
        return 1;
    }

    int pid = o.pid;
    if (!pid) {
        int found[8], n = viture_usb_find(found, 8), valid[8], nv = 0;
        for (int i = 0; i < n; i++)
            if (api.is_product_id_valid(found[i]) == 1)
                valid[nv++] = found[i];
        if (nv == 0) {
            fprintf(stderr,
                    "xrdome-pose-viture: no VITURE glasses found on USB (vendor 0x%04X). Plug them in, or "
                    "give their product id with --pid (0x1019 or 0x101D for the Pro).\n",
                    VITURE_USB_VENDOR);
            viture_unload(&api);
            return 1;
        }
        pid = valid[0];
        if (nv > 1)
            fprintf(stderr, "xrdome-pose-viture: %d VITURE devices found, using product id 0x%04X (--pid picks)\n",
                    nv, pid);
    } else if (api.is_product_id_valid(pid) != 1) {
        fprintf(stderr, "xrdome-pose-viture: the SDK does not know product id 0x%04X\n", pid);
        viture_unload(&api);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGBREAK
    signal(SIGBREAK, on_signal);
#endif

    /* The library stays loaded: the SDK may still have threads winding down,
     * and the process ends next. */
    return viture_bridge_run(&o, &api, pid, &g_stop, stderr, NULL);
}
