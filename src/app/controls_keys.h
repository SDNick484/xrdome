#ifndef XRD_APP_CONTROLS_KEYS_H
#define XRD_APP_CONTROLS_KEYS_H

#include <stdbool.h>
#include <stdint.h>

/* The SDL side of the controls (front end only; controls.h is the core's
 * view state). */

/* True for the keys that act on key repeat (an SDL keycode): zoom, seek, and
 * the depth strength and plane.
 * Every other key acts once per press (docs/usage.md, Keys). */
bool xrd_key_repeats(int32_t sdl_keycode);

#endif
