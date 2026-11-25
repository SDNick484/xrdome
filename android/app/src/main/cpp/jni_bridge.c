/* The JNI surface of libxrdome_jni.so (NativeBridge.kt). */
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices.h"
#include "gl_info.h"
#include "render_loop.h"

JNIEXPORT jstring JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_version(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return (*env)->NewStringUTF(env, XRD_VERSION);
}

/* The device report's [gl] section: see gl_info.c. Runs on the calling thread and leaves no
 * EGL context current there. */
JNIEXPORT jstring JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_glInfo(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    char buf[1024];
    xrd_gl_info(buf, sizeof buf);
    return (*env)->NewStringUTF(env, buf);
}

/* The core's FOV table (xrd_device_builtin), so the app and the Linux player agree. 0 for a name
 * the core does not know. */
JNIEXPORT jfloat JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_deviceFov(JNIEnv *env, jobject thiz, jstring name)
{
    (void)thiz;
    const char *s = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
    float fov = 0.0f;
    if (s) {
        if (!xrd_device_builtin(s, &fov))
            fov = 0.0f;
        (*env)->ReleaseStringUTFChars(env, name, s);
    }
    return fov;
}

/* --- The render loop (render_loop.c). Every call below runs on the render thread
 * (RenderThread.kt); the handle is a pointer to this struct. --- */

typedef struct {
    xrd_render_loop *rl;
    ANativeWindow *win; /* released after the loop, which draws into it */
} xrd_jni_loop;

/* The last error of create or draw, per thread: lastError() is read on the render thread. */
static _Thread_local char g_err[512];

static xrd_jni_loop *loop_of(jlong h)
{
    return (xrd_jni_loop *)(intptr_t)h;
}

JNIEXPORT jstring JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_lastError(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return (*env)->NewStringUTF(env, g_err);
}

JNIEXPORT jlong JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_create(JNIEnv *env, jobject thiz, jobject surface,
                                                    jint w, jint h, jfloat fov_diag)
{
    (void)thiz;
    g_err[0] = '\0';
    ANativeWindow *win = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (!win) {
        snprintf(g_err, sizeof g_err, "no native window for the glasses' surface");
        return 0;
    }
    xrd_jni_loop *l = calloc(1, sizeof *l);
    if (!l) {
        ANativeWindow_release(win);
        snprintf(g_err, sizeof g_err, "out of memory");
        return 0;
    }
    l->win = win;
    l->rl = xrd_render_loop_new(win, w, h, fov_diag, g_err, sizeof g_err);
    if (!l->rl) {
        ANativeWindow_release(win);
        free(l);
        return 0;
    }
    return (jlong)(intptr_t)l;
}

JNIEXPORT jint JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_oesTexture(JNIEnv *env, jobject thiz, jlong h)
{
    (void)env;
    (void)thiz;
    return h ? (jint)xrd_render_loop_oes_texture(loop_of(h)->rl) : 0;
}

JNIEXPORT void JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_setVideoSize(JNIEnv *env, jobject thiz, jlong h,
                                                          jint w, jint height)
{
    (void)env;
    (void)thiz;
    if (h)
        xrd_render_loop_set_video_size(loop_of(h)->rl, w, height);
}

JNIEXPORT void JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_showPattern(JNIEnv *env, jobject thiz, jlong h)
{
    (void)env;
    (void)thiz;
    if (h)
        xrd_render_loop_show_pattern(loop_of(h)->rl);
}

JNIEXPORT jboolean JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_draw(JNIEnv *env, jobject thiz, jlong h,
                                                  jboolean new_frame, jfloatArray st_matrix,
                                                  jlong frame_time_ns)
{
    (void)thiz;
    if (!h) {
        snprintf(g_err, sizeof g_err, "draw without a render loop");
        return JNI_FALSE;
    }
    /* Identity unless a full 4x4 came: the pattern needs no transform. */
    float st[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (st_matrix && (*env)->GetArrayLength(env, st_matrix) >= 16)
        (*env)->GetFloatArrayRegion(env, st_matrix, 0, 16, st);
    return xrd_render_loop_draw(loop_of(h)->rl, new_frame == JNI_TRUE, st, (int64_t)frame_time_ns,
                                g_err, sizeof g_err)
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_destroy(JNIEnv *env, jobject thiz, jlong h)
{
    (void)env;
    (void)thiz;
    xrd_jni_loop *l = loop_of(h);
    if (!l)
        return;
    xrd_render_loop_free(l->rl);
    ANativeWindow_release(l->win);
    free(l);
}
