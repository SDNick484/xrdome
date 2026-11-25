/* The JNI surface of libxrdome_jni.so (NativeBridge.kt). */
#include <jni.h>

#include "gl_info.h"

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
