package io.github.sdnick484.xrdome

/** The JNI surface of libxrdome_jni.so. Later tasks add functions here. */
object NativeBridge {
    init {
        System.loadLibrary("xrdome_jni")
    }

    /** XRD_VERSION from meson.build's project(version:), compiled into the core. */
    external fun version(): String
}
