#ifndef XRD_TEST_COMPAT_H
#define XRD_TEST_COMPAT_H

/* The few POSIX calls the unit tests use that Windows (MinGW-w64, UCRT)
 * lacks, so the same tests run there. Include after the system headers.
 * Elsewhere this only adds xrd_test_rmtree.
 *
 * Temporary files: the tests keep their "/tmp/..." templates. On Windows
 * that is \tmp on the current drive, created here on first use. */

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

/* As macros over xrd_test_* functions: some MinGW-w64 releases declare a
 * few of these already. */
#define setenv xrd_test_setenv
#define unsetenv xrd_test_unsetenv
#define mkdtemp xrd_test_mkdtemp
#define mkstemp xrd_test_mkstemp
#define symlink xrd_test_symlink

static inline int xrd_test_setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && getenv(name))
        return 0;
    /* _putenv_s with "" removes the variable: an empty value cannot be set
     * on Windows, and every test that sets one only means "not usable". */
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

static inline int xrd_test_unsetenv(const char *name)
{
    return _putenv_s(name, "") == 0 ? 0 : -1;
}

static inline void xrd_test_tmp_root(const char *tmpl)
{
    if (strncmp(tmpl, "/tmp/", 5) == 0)
        _mkdir("/tmp");
}

static inline char *xrd_test_mkdtemp(char *tmpl)
{
    xrd_test_tmp_root(tmpl);
    if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0 || _mkdir(tmpl) != 0)
        return NULL;
    return tmpl;
}

static inline int xrd_test_mkstemp(char *tmpl)
{
    xrd_test_tmp_root(tmpl);
    if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0)
        return -1;
    return _open(tmpl, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
}

/* Symlinks need a privilege on Windows; the tests that make one skip(). */
static inline int xrd_test_symlink(const char *target, const char *link)
{
    (void)target;
    (void)link;
    errno = ENOSYS;
    return -1;
}

/* realpath into a PATH_MAX buffer (the only form the tests use). */
#define realpath(path, resolved) _fullpath((resolved), (path), PATH_MAX)

/* mkdir(path, mode): Windows' takes no mode. */
#define mkdir(path, mode) _mkdir(path)

/* rm -rf, without a shell. */
static inline int xrd_test_rmtree(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    if (!S_ISDIR(st.st_mode)) {
        _chmod(path, _S_IREAD | _S_IWRITE);
        return remove(path) == 0 ? 0 : -1;
    }
    DIR *d = opendir(path);
    int rc = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            size_t n = strlen(path) + strlen(e->d_name) + 2;
            char *child = malloc(n);
            if (!child) {
                rc = -1;
                break;
            }
            snprintf(child, n, "%s/%s", path, e->d_name);
            if (xrd_test_rmtree(child) != 0)
                rc = -1;
            free(child);
        }
        closedir(d);
    }
    return _rmdir(path) == 0 && rc == 0 ? 0 : -1;
}

#else

static inline int xrd_test_rmtree(const char *path)
{
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", path);
    return system(cmd) == 0 ? 0 : -1;
}

#endif

#endif
