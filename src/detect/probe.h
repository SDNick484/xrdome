#ifndef XRD_PROBE_H
#define XRD_PROBE_H

#include <stdbool.h>
#include <stddef.h>

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

/* True for the name of an image demuxer, as ffprobe prints it in format_name:
 * "image2", "image2pipe" and every "<codec>_pipe" (jpeg_pipe, png_pipe,
 * webp_pipe, ...). Not "gif" (an animation), and not a container such as
 * "mov,mp4,m4a,3gp,3g2,mj2" whose video stream happens to be one frame. */
bool xrd_probe_format_is_image(const char *format_name);

/* Parses `ffprobe -v error -show_streams -show_format` text: the first
 * [STREAM] block with codec_type=video and DISPOSITION:attached_pic=0 (or no
 * such line). A block counts only when its closing [/STREAM] line is present,
 * so truncated output never yields half a stream. The [FORMAT] block, when
 * complete, sets image; text without one (just -show_streams) leaves it false.
 * False when there is no stream; *out is then zeroed with sar 1:1. */
bool xrd_probe_parse(const char *text, xrd_probe *out);

#define XRD_PROBE_TIMEOUT_MS 3000
#define XRD_PROBE_MAX_BYTES (256 * 1024)
typedef enum { XRD_PROBE_OK, XRD_PROBE_SKIPPED, XRD_PROBE_FAILED } xrd_probe_status;

/* Runs: <ffprobe> -v error -show_streams -show_format -i <file>   (execvp; stdin and stderr
 * to /dev/null). SKIPPED when file is not a regular file (no err). FAILED, with
 * err set, when the program cannot be started, exits non-zero, exceeds
 * timeout_ms (it is killed and reaped), prints more than XRD_PROBE_MAX_BYTES,
 * or prints no video stream. *out is zeroed (sar 1:1) unless the status is OK. */
xrd_probe_status xrd_probe_run(const char *ffprobe, const char *file, int timeout_ms,
                               xrd_probe *out, char *err, size_t errlen);

#endif
