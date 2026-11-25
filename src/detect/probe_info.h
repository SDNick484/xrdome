#ifndef XRD_PROBE_INFO_H
#define XRD_PROBE_INFO_H

#include <stdbool.h>

/* What a probe found out about a file, without the probing (probe.h runs
 * ffprobe; the core, detect.c, reads only this). */

typedef enum { XRD_STEREO_NONE, XRD_STEREO_2D, XRD_STEREO_SBS, XRD_STEREO_TB,
               XRD_STEREO_OTHER } xrd_probe_stereo;
typedef enum { XRD_SPH_NONE, XRD_SPH_EQUIRECT, XRD_SPH_OTHER } xrd_probe_spherical;

typedef struct {
    bool have_size;            /* width and height are both > 0 */
    int width, height;         /* coded frame, pixels */
    int sar_num, sar_den;      /* 1:1 when absent, N/A or non-positive */
    xrd_probe_stereo stereo;
    bool stereo_inverted;
    char stereo_name[32];      /* ffprobe's own word, for notes */
    xrd_probe_spherical spherical;
    char spherical_name[32];
    bool image;                /* the container is an image demuxer (see xrd_probe_format_is_image) */
} xrd_probe;

/* *p as after a failed parse: zeroed, sar 1:1. */
void xrd_probe_zero(xrd_probe *p);

#endif
