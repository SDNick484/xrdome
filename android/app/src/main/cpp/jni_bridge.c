/* The JNI surface of libxrdome_jni.so (NativeBridge.kt). */
#include <jni.h>

JNIEXPORT jstring JNICALL
Java_io_github_sdnick484_xrdome_NativeBridge_version(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return (*env)->NewStringUTF(env, XRD_VERSION);
}
