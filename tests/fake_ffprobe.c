/* Stand-in for ffprobe in tests/test_probe_run.c, a program so it runs the
 * same way on every platform (Windows has no /bin/sh). Behaviour comes from
 * $FAKE_FFPROBE:
 *   ok     print $FAKE_FFPROBE_FILE
 *   argv   write the arguments, one per line, to $FAKE_FFPROBE_OUT, then print the fixture
 *   hang   sleep 30 s
 *   flood  print an endless stream of x
 *   fail   exit 3
 *   segv   crash (SIGSEGV; Windows: an access violation) */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <time.h>
#endif

static int cat(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f)
        return 98;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        fwrite(buf, 1, n, stdout);
    fclose(f);
    return fflush(stdout) == 0 ? 0 : 97;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); /* the fixture's bytes, no "\r\n" */
#endif
    const char *mode = getenv("FAKE_FFPROBE");
    if (!mode)
        return 99;
    if (!strcmp(mode, "ok"))
        return cat(getenv("FAKE_FFPROBE_FILE"));
    if (!strcmp(mode, "argv")) {
        const char *out = getenv("FAKE_FFPROBE_OUT");
        FILE *f = out ? fopen(out, "wb") : NULL;
        if (!f)
            return 98;
        for (int i = 1; i < argc; i++)
            fprintf(f, "%s\n", argv[i]);
        if (fclose(f) != 0)
            return 97;
        return cat(getenv("FAKE_FFPROBE_FILE"));
    }
    if (!strcmp(mode, "hang")) {
#ifdef _WIN32
        Sleep(30000);
#else
        struct timespec ts = { 30, 0 };
        nanosleep(&ts, NULL);
#endif
        return 0;
    }
    if (!strcmp(mode, "flood")) {
        static const char x[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n";
        while (fwrite(x, 1, sizeof x - 1, stdout) == sizeof x - 1)
            ;
        return 1; /* the reader went away */
    }
    if (!strcmp(mode, "fail"))
        return 3;
    if (!strcmp(mode, "segv")) {
#ifdef _WIN32
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX); /* no WER dialog */
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, NULL);
#else
        raise(SIGSEGV);
#endif
        return 1;
    }
    return 99;
}
