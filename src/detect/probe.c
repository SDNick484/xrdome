#define _POSIX_C_SOURCE 200809L

#include "detect/probe.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void probe_zero(xrd_probe *p)
{
    memset(p, 0, sizeof *p);
    p->sar_num = 1;
    p->sar_den = 1;
}

static void copy_name(char *dst, size_t n, const char *src, size_t len)
{
    if (len >= n)
        len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Positive decimal integer filling [s, s+len); false otherwise. */
static bool parse_pos(const char *s, size_t len, long *out)
{
    if (len == 0 || len > 9)
        return false;
    long v = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
        v = v * 10 + (s[i] - '0');
    }
    if (v <= 0)
        return false;
    *out = v;
    return true;
}

typedef enum { SEC_NONE, SEC_STREAM, SEC_SIDE } section;
typedef enum { SIDE_OTHER, SIDE_STEREO, SIDE_SPHERICAL } side_kind;

bool xrd_probe_parse(const char *text, xrd_probe *out)
{
    probe_zero(out);
    if (!text)
        return false;

    section sec = SEC_NONE;
    side_kind side = SIDE_OTHER;
    xrd_probe cur;
    bool is_video = false, attached = false;
    probe_zero(&cur);

    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        const char *next = eol ? eol + 1 : p + len;
        if (len && p[len - 1] == '\r')
            len--;

        if (len == 8 && !memcmp(p, "[STREAM]", 8)) {
            sec = SEC_STREAM;
            probe_zero(&cur);
            is_video = attached = false;
        } else if (len == 9 && !memcmp(p, "[/STREAM]", 9)) {
            if (sec != SEC_NONE && is_video && !attached) {
                *out = cur;
                return true;
            }
            sec = SEC_NONE;
        } else if (sec != SEC_NONE && len == 11 && !memcmp(p, "[SIDE_DATA]", 11)) {
            sec = SEC_SIDE;
            side = SIDE_OTHER;
        } else if (sec == SEC_SIDE && len == 12 && !memcmp(p, "[/SIDE_DATA]", 12)) {
            sec = SEC_STREAM;
        } else if (sec != SEC_NONE) {
            const char *eq = memchr(p, '=', len);
            if (eq) {
                size_t klen = (size_t)(eq - p);
                const char *v = eq + 1;
                size_t vlen = len - klen - 1;
                #define KEY_IS(s) (klen == sizeof(s) - 1 && !memcmp(p, s, klen))
                #define VAL_IS(s) (vlen == sizeof(s) - 1 && !memcmp(v, s, vlen))
                if (sec == SEC_STREAM) {
                    long n;
                    if (KEY_IS("codec_type")) {
                        is_video = VAL_IS("video");
                    } else if (KEY_IS("DISPOSITION:attached_pic")) {
                        attached = VAL_IS("1");
                    } else if (KEY_IS("width")) {
                        cur.width = parse_pos(v, vlen, &n) ? (int)n : 0;
                    } else if (KEY_IS("height")) {
                        cur.height = parse_pos(v, vlen, &n) ? (int)n : 0;
                    } else if (KEY_IS("sample_aspect_ratio")) {
                        const char *colon = memchr(v, ':', vlen);
                        long a, b;
                        if (colon && parse_pos(v, (size_t)(colon - v), &a) &&
                            parse_pos(colon + 1, vlen - (size_t)(colon - v) - 1, &b)) {
                            cur.sar_num = (int)a;
                            cur.sar_den = (int)b;
                        } else {
                            cur.sar_num = cur.sar_den = 1;
                        }
                    }
                    cur.have_size = cur.width > 0 && cur.height > 0;
                } else { /* SEC_SIDE */
                    if (KEY_IS("side_data_type")) {
                        if (VAL_IS("Stereo 3D"))
                            side = SIDE_STEREO;
                        else if (VAL_IS("Spherical Mapping"))
                            side = SIDE_SPHERICAL;
                        else
                            side = SIDE_OTHER;
                    } else if (side == SIDE_STEREO && KEY_IS("type")) {
                        copy_name(cur.stereo_name, sizeof cur.stereo_name, v, vlen);
                        if (VAL_IS("side by side"))
                            cur.stereo = XRD_STEREO_SBS;
                        else if (VAL_IS("top and bottom"))
                            cur.stereo = XRD_STEREO_TB;
                        else if (VAL_IS("2D"))
                            cur.stereo = XRD_STEREO_2D;
                        else
                            cur.stereo = XRD_STEREO_OTHER;
                    } else if (side == SIDE_STEREO && KEY_IS("inverted")) {
                        cur.stereo_inverted = VAL_IS("1");
                    } else if (side == SIDE_SPHERICAL && KEY_IS("projection")) {
                        copy_name(cur.spherical_name, sizeof cur.spherical_name, v, vlen);
                        char tmp[64];
                        copy_name(tmp, sizeof tmp, v, vlen);
                        cur.spherical = strstr(tmp, "equirectangular") ? XRD_SPH_EQUIRECT
                                                                       : XRD_SPH_OTHER;
                    }
                }
                #undef KEY_IS
                #undef VAL_IS
            }
        }
        p = next;
    }
    return false;
}

/* ---- running ffprobe ---------------------------------------------------- */

/* pipe() with both ends close-on-exec (pipe2 needs _GNU_SOURCE). */
static int cloexec_pipe(int fds[2])
{
    if (pipe(fds) != 0)
        return -1;
    for (int i = 0; i < 2; i++) {
        if (fcntl(fds[i], F_SETFD, FD_CLOEXEC) != 0) {
            close(fds[0]);
            close(fds[1]);
            return -1;
        }
    }
    return 0;
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void fail_msg(char *err, size_t errlen, const char *what, const char *prog)
{
    if (err && errlen)
        snprintf(err, errlen, "%s %s", what, prog);
}

/* Waits for pid until deadline (ms, monotonic); SIGKILLs it then. Always reaps.
 * Returns the wait status, with *killed set when the deadline forced the kill. */
static int reap(pid_t pid, long long deadline, bool *killed)
{
    int status = 0;
    *killed = false;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            return status;
        if (r < 0 && errno != EINTR)
            return 0;
        if (now_ms() >= deadline)
            break;
        struct timespec ts = { 0, 1000000 };
        nanosleep(&ts, NULL);
    }
    kill(pid, SIGKILL);
    *killed = true;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    return status;
}

xrd_probe_status xrd_probe_run(const char *ffprobe, const char *file, int timeout_ms,
                               xrd_probe *out, char *err, size_t errlen)
{
    probe_zero(out);
    if (err && errlen)
        err[0] = '\0';

    struct stat st;
    if (!file || !ffprobe || stat(file, &st) != 0 || !S_ISREG(st.st_mode))
        return XRD_PROBE_SKIPPED;

    int outp[2], errp[2];
    if (cloexec_pipe(outp) != 0) {
        fail_msg(err, errlen, "cannot create a pipe to run", ffprobe);
        return XRD_PROBE_FAILED;
    }
    if (cloexec_pipe(errp) != 0) {
        close(outp[0]);
        close(outp[1]);
        fail_msg(err, errlen, "cannot create a pipe to run", ffprobe);
        return XRD_PROBE_FAILED;
    }

    long long deadline = now_ms() + timeout_ms;
    pid_t pid = fork();
    if (pid < 0) {
        close(outp[0]); close(outp[1]); close(errp[0]); close(errp[1]);
        fail_msg(err, errlen, "cannot fork to run", ffprobe);
        return XRD_PROBE_FAILED;
    }
    if (pid == 0) {
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) {
            dup2(dn, 0);
            dup2(dn, 2);
            if (dn > 2)
                close(dn);
        }
        dup2(outp[1], 1);
        char *argv[] = { (char *)ffprobe, "-v", "error", "-show_streams", "-i", (char *)file, NULL };
        execvp(ffprobe, argv);
        int e = errno;
        ssize_t w = write(errp[1], &e, sizeof e);
        (void)w;
        _exit(127);
    }
    close(outp[1]);
    close(errp[1]);

    /* An exec failure shows up as an errno on errp; a successful exec closes it. */
    int exec_errno = 0;
    {
        ssize_t r;
        do { r = read(errp[0], &exec_errno, sizeof exec_errno); } while (r < 0 && errno == EINTR);
        if (r != (ssize_t)sizeof exec_errno)
            exec_errno = 0;
    }
    close(errp[0]);

    char *buf = malloc((size_t)XRD_PROBE_MAX_BYTES + 1);
    size_t len = 0;
    bool overflow = false, timed_out = false;
    if (!buf) {
        close(outp[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        fail_msg(err, errlen, "out of memory running", ffprobe);
        return XRD_PROBE_FAILED;
    }

    while (!exec_errno) {
        long long left = deadline - now_ms();
        if (left <= 0) { timed_out = true; break; }
        struct pollfd pfd = { outp[0], POLLIN, 0 };
        int pr = poll(&pfd, 1, left > INT_MAX ? INT_MAX : (int)left);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) { timed_out = true; break; }
        ssize_t n = read(outp[0], buf + len, (size_t)XRD_PROBE_MAX_BYTES + 1 - len);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0)
            break;
        len += (size_t)n;
        if (len > XRD_PROBE_MAX_BYTES) { overflow = true; break; }
    }
    close(outp[0]);
    buf[len > XRD_PROBE_MAX_BYTES ? XRD_PROBE_MAX_BYTES : len] = '\0';

    int status;
    bool killed;
    if (timed_out || overflow) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
    } else {
        status = reap(pid, deadline, &killed);
        timed_out = killed;
    }

    xrd_probe_status result = XRD_PROBE_FAILED;
    if (exec_errno) {
        if (err && errlen)
            snprintf(err, errlen, "cannot run %s: %s", ffprobe, strerror(exec_errno));
    } else if (timed_out) {
        if (err && errlen)
            snprintf(err, errlen, "%s timed out after %d ms", ffprobe, timeout_ms);
    } else if (overflow) {
        if (err && errlen)
            snprintf(err, errlen, "%s printed more than %d bytes", ffprobe, XRD_PROBE_MAX_BYTES);
    } else if (WIFSIGNALED(status)) {
        if (err && errlen)
            snprintf(err, errlen, "%s was killed by signal %d", ffprobe, WTERMSIG(status));
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (err && errlen)
            snprintf(err, errlen, "%s exited with status %d", ffprobe,
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    } else if (!xrd_probe_parse(buf, out)) {
        if (err && errlen)
            snprintf(err, errlen, "%s found no video stream", ffprobe);
    } else {
        result = XRD_PROBE_OK;
    }
    if (result != XRD_PROBE_OK)
        probe_zero(out);
    free(buf);
    return result;
}
