#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* dlopen */
#endif

#include "viture_loader.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/* Compile-time check that each pointer type above is the header's prototype.
 * _Generic does not evaluate its controlling expression, so this refers to no
 * symbol and nothing needs linking. */
#define CHECK_SIG(fn, member) \
    _Static_assert(_Generic(&fn, __typeof__(((viture_api *)0)->member) : 1, default : 0), \
                   #fn ": the SDK header's prototype differs from viture_api")

CHECK_SIG(xr_device_provider_create, create);
CHECK_SIG(xr_device_provider_initialize, initialize);
CHECK_SIG(xr_device_provider_start, start);
CHECK_SIG(xr_device_provider_stop, stop);
CHECK_SIG(xr_device_provider_shutdown, shutdown);
CHECK_SIG(xr_device_provider_destroy, destroy);
CHECK_SIG(xr_device_provider_get_device_type, get_device_type);
CHECK_SIG(xr_device_provider_is_product_id_valid, is_product_id_valid);
CHECK_SIG(xr_device_provider_is_product_support_imu_frequency, is_product_support_imu_frequency);
CHECK_SIG(xr_device_provider_register_imu_pose_callback, register_imu_pose_callback);
CHECK_SIG(xr_device_provider_open_imu, open_imu);
CHECK_SIG(xr_device_provider_close_imu, close_imu);
CHECK_SIG(xr_device_provider_get_display_mode, get_display_mode);
CHECK_SIG(xr_device_provider_set_display_mode, set_display_mode);

const char *viture_default_lib(void)
{
#if defined(_WIN32)
    return "libglasses.dll";
#elif defined(__APPLE__)
    return "libglasses.dylib";
#else
    return "libglasses.so";
#endif
}

#ifdef _WIN32
/* UTF-8 to a malloc'd wide string; NULL on failure. */
static wchar_t *widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0)
        return NULL;
    wchar_t *w = malloc((size_t)n * sizeof *w);
    if (w && MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

static void *lib_open(const char *path, char *err, size_t errlen)
{
    wchar_t *w = widen(path);
    if (!w) {
        snprintf(err, errlen, "cannot load library \"%.200s\": not a valid UTF-8 path", path);
        return NULL;
    }
    HMODULE h;
    /* A path with a folder in it is loaded with the DLL's own folder first in
     * the search for its dependencies (VITURE's DLL sits beside them). */
    if (wcschr(w, L'\\') || wcschr(w, L'/')) {
        wchar_t full[MAX_PATH * 4];
        DWORD n = GetFullPathNameW(w, (DWORD)(sizeof full / sizeof full[0]), full, NULL);
        h = n > 0 && n < sizeof full / sizeof full[0]
                ? LoadLibraryExW(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)
                : LoadLibraryW(w);
    } else {
        h = LoadLibraryW(w);
    }
    free(w);
    if (!h)
        snprintf(err, errlen, "cannot load library \"%.200s\": Windows error %lu", path,
                 (unsigned long)GetLastError());
    return h;
}

static void *lib_sym(void *lib, const char *name)
{
    FARPROC p = GetProcAddress((HMODULE)lib, name);
    void *v;
    memcpy(&v, &p, sizeof v);
    return v;
}

static void lib_close(void *lib)
{
    FreeLibrary((HMODULE)lib);
}
#else
static void *lib_open(const char *path, char *err, size_t errlen)
{
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char *e = dlerror();
        snprintf(err, errlen, "cannot load library \"%.200s\": %.200s", path, e ? e : "unknown error");
    }
    return h;
}

static void *lib_sym(void *lib, const char *name)
{
    return dlsym(lib, name);
}

static void lib_close(void *lib)
{
    dlclose(lib);
}
#endif

/* Stores the symbol into the function pointer at dst. Data and function
 * pointers are different types to ISO C, so this goes through memcpy. */
static int resolve(void *lib, const char *path, const char *name, void *dst, char *err, size_t errlen)
{
    void *p = lib_sym(lib, name);
    if (!p) {
        snprintf(err, errlen,
                 "library \"%.200s\" has no symbol \"%s\" (is it VITURE's Glasses SDK, version 2 or later?)",
                 path, name);
        return -1;
    }
    memcpy(dst, &p, sizeof p);
    return 0;
}

int viture_load(const char *path, viture_api *api, char *err, size_t errlen)
{
    if (!path || !path[0])
        path = viture_default_lib();
    memset(api, 0, sizeof *api);
    api->lib = lib_open(path, err, errlen);
    if (!api->lib)
        return -1;

#define SYM(field, name) \
    if (resolve(api->lib, path, name, &api->field, err, errlen) != 0) \
        goto fail
    SYM(create, "xr_device_provider_create");
    SYM(initialize, "xr_device_provider_initialize");
    SYM(start, "xr_device_provider_start");
    SYM(stop, "xr_device_provider_stop");
    SYM(shutdown, "xr_device_provider_shutdown");
    SYM(destroy, "xr_device_provider_destroy");
    SYM(get_device_type, "xr_device_provider_get_device_type");
    SYM(is_product_id_valid, "xr_device_provider_is_product_id_valid");
    SYM(is_product_support_imu_frequency, "xr_device_provider_is_product_support_imu_frequency");
    SYM(register_imu_pose_callback, "xr_device_provider_register_imu_pose_callback");
    SYM(open_imu, "xr_device_provider_open_imu");
    SYM(close_imu, "xr_device_provider_close_imu");
    SYM(get_display_mode, "xr_device_provider_get_display_mode");
    SYM(set_display_mode, "xr_device_provider_set_display_mode");
#undef SYM
    return 0;

fail:
    lib_close(api->lib);
    memset(api, 0, sizeof *api);
    return -1;
}

void viture_unload(viture_api *api)
{
    if (api->lib)
        lib_close(api->lib);
    memset(api, 0, sizeof *api);
}
