/* Running ffprobe on Windows (CreateProcessW), and the command-line quoting
 * it needs. The quoting is pure and built on every platform so it is unit
 * tested on any host; the runner is Windows only (probe.c has the POSIX one). */

#include "detect/probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>
#endif

/* ---- quoting ------------------------------------------------------------- */

static bool put(char *buf, size_t len, size_t *n, char c, size_t count)
{
    if (count >= len - *n)
        return false;
    memset(buf + *n, c, count);
    *n += count;
    buf[*n] = '\0';
    return true;
}

bool xrd_win_quote_arg(const char *arg, char *buf, size_t len, size_t *n)
{
    if (!arg || !buf || *n >= len)
        return false;
    if (arg[0] != '\0' && !strpbrk(arg, " \t\n\v\"")) {
        size_t a = strlen(arg);
        if (a >= len - *n)
            return false;
        memcpy(buf + *n, arg, a + 1);
        *n += a;
        return true;
    }

    if (!put(buf, len, n, '"', 1))
        return false;
    for (const char *p = arg;; p++) {
        size_t slashes = 0;
        while (*p == '\\') {
            slashes++;
            p++;
        }
        if (*p == '\0') /* before the closing quote: doubled */
            return put(buf, len, n, '\\', slashes * 2) && put(buf, len, n, '"', 1);
        if (*p == '"') { /* doubled, and one more to escape the quote */
            if (!put(buf, len, n, '\\', slashes * 2 + 1) || !put(buf, len, n, '"', 1))
                return false;
        } else if (!put(buf, len, n, '\\', slashes) || !put(buf, len, n, *p, 1)) {
            return false;
        }
    }
}

int xrd_win_command_line(const char *const *argv, char *buf, size_t len)
{
    if (!argv || !argv[0] || argv[0][0] == '\0' || strchr(argv[0], '"') || !buf || len == 0)
        return -1;
    size_t n = 0;
    buf[0] = '\0';
    /* argv[0]: backslashes are literal there, quotes only group. */
    bool q = strpbrk(argv[0], " \t") != NULL;
    size_t a = strlen(argv[0]);
    if (a + (q ? 2 : 0) >= len)
        return -1;
    n = (size_t)snprintf(buf, len, q ? "\"%s\"" : "%s", argv[0]);
    for (size_t i = 1; argv[i]; i++) {
        if (!put(buf, len, &n, ' ', 1) || !xrd_win_quote_arg(argv[i], buf, len, &n))
            return -1;
    }
    return 0;
}

/* ---- running ffprobe (Windows) ------------------------------------------- */

#ifdef _WIN32

/* UTF-8 to a malloc'd wide string, NULL on failure. */
static wchar_t *widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (w && MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}

/* The system's message for code, one line. */
static void win_message(DWORD code, char *buf, size_t len)
{
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                             code, 0, buf, len > 0xffff ? 0xffff : (DWORD)len, NULL);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' ||
                     buf[n - 1] == '.'))
        buf[--n] = '\0';
    if (n == 0)
        snprintf(buf, len, "error %lu", (unsigned long)code);
}

xrd_probe_status xrd_probe_run(const char *ffprobe, const char *file, int timeout_ms,
                               xrd_probe *out, char *err, size_t errlen)
{
    xrd_probe_zero(out);
    if (err && errlen)
        err[0] = '\0';

    struct stat st;
    if (!file || !ffprobe || stat(file, &st) != 0 || !S_ISREG(st.st_mode))
        return XRD_PROBE_SKIPPED;

    const char *argv[] = { ffprobe, "-v", "error", "-show_streams", "-show_format", "-i",
                           file, NULL };
    char line[32768]; /* CreateProcessW's command-line limit, in characters */
    wchar_t *wline = NULL;
    if (xrd_win_command_line(argv, line, sizeof line) != 0 || !(wline = widen(line))) {
        if (err && errlen)
            snprintf(err, errlen, "cannot run %s: the command line cannot be built", ffprobe);
        return XRD_PROBE_FAILED;
    }

    /* stdout through a pipe; stdin and stderr to NUL, as /dev/null on POSIX
     * (ffprobe's messages are not read, so no second pipe can fill up). */
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL, nul = INVALID_HANDLE_VALUE;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    PROCESS_INFORMATION pi = { 0 };
    char *buf = NULL;
    bool started = false;
    xrd_probe_status result = XRD_PROBE_FAILED;

    if (!CreatePipe(&rd, &wr, &sa, 0) || !SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0)) {
        if (err && errlen)
            snprintf(err, errlen, "cannot create a pipe to run %s", ffprobe);
        goto done;
    }
    nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      &sa, OPEN_EXISTING, 0, NULL);
    if (nul == INVALID_HANDLE_VALUE) {
        if (err && errlen)
            snprintf(err, errlen, "cannot open NUL to run %s", ffprobe);
        goto done;
    }

    /* Only these two handles are inherited, not every inheritable handle
     * the process happens to hold. */
    SIZE_T asize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &asize);
    attrs = malloc(asize);
    HANDLE inherit[2] = { wr, nul };
    if (attrs && !InitializeProcThreadAttributeList(attrs, 1, 0, &asize)) {
        free(attrs);
        attrs = NULL;
    }
    if (!attrs || !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                             inherit, sizeof inherit, NULL, NULL)) {
        if (err && errlen)
            snprintf(err, errlen, "cannot set up the handles to run %s", ffprobe);
        goto done;
    }

    STARTUPINFOEXW si;
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul;
    si.StartupInfo.hStdOutput = wr;
    si.StartupInfo.hStdError = nul;
    si.lpAttributeList = attrs;

    ULONGLONG t0 = GetTickCount64();
    /* NULL application name: the first token of the command line is looked
     * up as Windows does (the program's directory, the current one, the
     * system directories, PATH; ".exe" appended). */
    if (!CreateProcessW(NULL, wline, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                        &si.StartupInfo, &pi)) {
        char msg[256];
        win_message(GetLastError(), msg, sizeof msg);
        if (err && errlen)
            snprintf(err, errlen, "cannot run %s: %s", ffprobe, msg);
        goto done;
    }
    started = true;
    CloseHandle(wr); /* ours: the pipe ends when ffprobe's copy closes */
    wr = NULL;

    buf = malloc((size_t)XRD_PROBE_MAX_BYTES + 1);
    if (!buf) {
        if (err && errlen)
            snprintf(err, errlen, "out of memory running %s", ffprobe);
        goto done;
    }

    /* Anonymous pipes cannot be read with a timeout: peek, read what is
     * there, else wait on the process for at most 1 ms. A broken pipe (the
     * child exited and everything is read) ends it. */
    size_t len = 0;
    bool overflow = false, timed_out = false;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL))
            break; /* ERROR_BROKEN_PIPE: end of output */
        if (avail > 0) {
            DWORD room = (DWORD)((size_t)XRD_PROBE_MAX_BYTES + 1 - len), got = 0;
            if (!ReadFile(rd, buf + len, avail < room ? avail : room, &got, NULL))
                break;
            len += got;
            if (len > XRD_PROBE_MAX_BYTES) {
                overflow = true;
                break;
            }
            continue;
        }
        if (GetTickCount64() - t0 >= (ULONGLONG)timeout_ms) {
            timed_out = true;
            break;
        }
        WaitForSingleObject(pi.hProcess, 1);
    }
    buf[len > XRD_PROBE_MAX_BYTES ? XRD_PROBE_MAX_BYTES : len] = '\0';

    /* The output ended: the process is exiting; give it until the deadline. */
    if (!timed_out && !overflow) {
        ULONGLONG spent = GetTickCount64() - t0;
        DWORD left = spent < (ULONGLONG)timeout_ms ? (DWORD)((ULONGLONG)timeout_ms - spent) : 0;
        if (WaitForSingleObject(pi.hProcess, left) != WAIT_OBJECT_0)
            timed_out = true;
    }
    DWORD code = 0;
    if (timed_out || overflow) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    } else if (!GetExitCodeProcess(pi.hProcess, &code)) {
        code = (DWORD)-1;
    }

    if (timed_out) {
        if (err && errlen)
            snprintf(err, errlen, "%s timed out after %d ms", ffprobe, timeout_ms);
    } else if (overflow) {
        if (err && errlen)
            snprintf(err, errlen, "%s printed more than %d bytes", ffprobe, XRD_PROBE_MAX_BYTES);
    } else if (code >= 0xC0000000u) { /* an NTSTATUS: the process crashed */
        if (err && errlen)
            snprintf(err, errlen, "%s crashed with status 0x%08lx", ffprobe,
                     (unsigned long)code);
    } else if (code != 0) {
        if (err && errlen)
            snprintf(err, errlen, "%s exited with status %lu", ffprobe, (unsigned long)code);
    } else if (!xrd_probe_parse(buf, out)) {
        if (err && errlen)
            snprintf(err, errlen, "%s found no video stream", ffprobe);
    } else {
        result = XRD_PROBE_OK;
    }

done:
    if (started && result != XRD_PROBE_OK && !buf) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    if (pi.hProcess)
        CloseHandle(pi.hProcess);
    if (pi.hThread)
        CloseHandle(pi.hThread);
    if (attrs) {
        DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }
    if (rd)
        CloseHandle(rd);
    if (wr)
        CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    free(wline);
    free(buf);
    if (result != XRD_PROBE_OK)
        xrd_probe_zero(out);
    return result;
}

#endif /* _WIN32 */
