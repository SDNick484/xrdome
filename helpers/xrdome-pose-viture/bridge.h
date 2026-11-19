#ifndef XRD_VITURE_BRIDGE_H
#define XRD_VITURE_BRIDGE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

#include "options.h"
#include "viture_loader.h"

/* IMU report frequencies the SDK knows (VITURE_IMU_FREQUENCY_LOW to
 * _ULTRA_HIGH, indexes 0 to 5) and what they mean in hertz. */
#define VITURE_FREQ_COUNT 6

/* The IMU frequency index to ask for: the lowest in mask (bit i = index i is
 * supported) that reports at least twice rate_hz, so a cap never has to wait
 * for a sample; else the highest in mask. -1 when mask is 0. */
int viture_pick_frequency(double rate_hz, unsigned mask);

/* The display mode for --sbs when the glasses are in current: 3840x1080 at 60
 * Hz, the mode VITURE's own 2D/3D switch uses. Returns false when current
 * already is a 3840x1080 mode (nothing to switch, nothing to restore). */
bool viture_sbs_target(int current, int *target);

/* Runs one session on the glasses with USB product id pid: creates and starts
 * the SDK, switches to SBS when asked, sends each pose (capped at o->rate) to
 * o->host:o->port, and runs until *stop is nonzero or o->count packets have
 * gone out. It then closes the IMU, puts the display mode back, and shuts the
 * SDK down. Messages go to out. Returns 0 after a clean session, 1 on an
 * error (the message is printed). *sent, when not NULL, gets the packet count. */
int viture_bridge_run(const viture_options *o, const viture_api *api, int pid, atomic_int *stop,
                      FILE *out, long *sent);

#endif
