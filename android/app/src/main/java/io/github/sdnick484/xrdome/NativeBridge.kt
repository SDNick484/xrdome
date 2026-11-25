package io.github.sdnick484.xrdome

import android.view.Surface

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

    /** The core's diagonal FOV for a built-in device name ("viture-pro"), 0 if it has none. */
    external fun deviceFov(name: String): Float

    // The render loop (render_loop.c). Every call below runs on the render thread
    // (play/RenderThread.kt), which keeps the EGL context current.

    /**
     * Makes an EGL ES 3.0 context and a window surface on [surface] (the glasses' Presentation,
     * [w] x [h]), the shared renderer, the OES texture and the copy pass. Returns the handle, or 0
     * on failure with the reason in [lastError].
     */
    external fun create(surface: Surface, w: Int, h: Int, fovDiag: Float): Long

    /** The GL_TEXTURE_EXTERNAL_OES texture name for SurfaceTexture(name). */
    external fun oesTexture(h: Long): Int

    /** The decoder's output size: the copy target is made this size on the next new frame. */
    external fun setVideoSize(h: Long, w: Int, height: Int)

    /** Draw the static SBS test pattern instead of the video from now on. */
    external fun showPattern(h: Long)

    /**
     * One vsync: with [newFrame] (updateTexImage was just called) copy the OES texture through
     * [stMatrix] (SurfaceTexture.getTransformMatrix) into the RGB10_A2 copy target, draw both
     * eyes, swap. False when the copy or the swap failed (the reason in [lastError]).
     */
    external fun draw(h: Long, newFrame: Boolean, stMatrix: FloatArray, frameTimeNs: Long): Boolean

    /** Releases the context, the surface and everything [create] made. 0 is accepted. */
    external fun destroy(h: Long)

    /** The reason the last [create] or [draw] on this thread failed. */
    external fun lastError(): String
}
