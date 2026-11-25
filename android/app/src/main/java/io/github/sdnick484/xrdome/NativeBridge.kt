package io.github.sdnick484.xrdome

/** The JNI surface of libxrdome_jni.so. Later tasks add functions here. */
object NativeBridge {
    init {
        System.loadLibrary("xrdome_jni")
    }

    /** XRD_VERSION from meson.build's project(version:), compiled into the core. */
    external fun version(): String

    /**
     * GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION and the essl3 OES extension, as
     * "NAME: value" lines, read from a throwaway pbuffer ES 3.0 context (gl_info.c). One
     * "error: ..." line if no such context could be made.
     */
    external fun glInfo(): String
}
