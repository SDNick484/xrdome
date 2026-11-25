package io.github.sdnick484.xrdome.play

import android.graphics.SurfaceTexture
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.util.Log
import android.view.Choreographer
import android.view.Surface
import io.github.sdnick484.xrdome.NativeBridge
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The render thread (spec 5.3 GL, Frame pacing): owns the EGL context (render_loop.c, through
 * [NativeBridge]) and the decoder's [SurfaceTexture], and draws once per Choreographer frame,
 * latching a new video frame first when the SurfaceTexture has one. Every NativeBridge render
 * call happens on this thread.
 *
 * Choreographer ticks at the phone screen's vsync; eglSwapBuffers (swap interval 1) blocks on the
 * glasses' queue, so a faster phone screen does not draw the glasses faster than 60 Hz.
 */
class RenderThread(
    private val surface: Surface,
    val width: Int,
    val height: Int,
    private val fovDiag: Float,
    private val listener: Listener,
) : HandlerThread("xrdome-render", Process.THREAD_PRIORITY_DISPLAY),
    Choreographer.FrameCallback,
    SurfaceTexture.OnFrameAvailableListener {

    /** Called on the render thread. */
    interface Listener {
        /** The SurfaceTexture the decoder renders into (Task 6: `Surface(surfaceTexture)`). */
        fun onSurfaceTexture(surfaceTexture: SurfaceTexture) {}

        /** create or draw failed; nothing is drawn any more. [finish] still has to be called. */
        fun onRenderError(message: String)
    }

    private lateinit var handler: Handler
    private var handle = 0L
    private var surfaceTexture: SurfaceTexture? = null
    private val frameAvailable = AtomicBoolean(false)
    private val stMatrix = FloatArray(16).also { android.opengl.Matrix.setIdentityM(it, 0) }
    private var running = false

    /** Starts the thread; it makes the context and, with [pattern], shows the test pattern. */
    fun begin(pattern: Boolean) {
        start()
        handler = Handler(looper)
        handler.post { open(pattern) }
    }

    private fun open(pattern: Boolean) {
        handle = NativeBridge.create(surface, width, height, fovDiag)
        if (handle == 0L) {
            listener.onRenderError(NativeBridge.lastError())
            return
        }
        if (pattern) NativeBridge.showPattern(handle)
        val st = SurfaceTexture(NativeBridge.oesTexture(handle))
        st.setOnFrameAvailableListener(this, handler)
        surfaceTexture = st
        listener.onSurfaceTexture(st)
        running = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture) {
        frameAvailable.set(true)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return
        var newFrame = false
        if (frameAvailable.getAndSet(false)) {
            val st = surfaceTexture ?: return
            try {
                st.updateTexImage()
                st.getTransformMatrix(stMatrix)
                newFrame = true
            } catch (e: RuntimeException) {
                Log.w(TAG, "updateTexImage failed", e)
            }
        }
        if (!NativeBridge.draw(handle, newFrame, stMatrix, frameTimeNanos)) {
            running = false
            listener.onRenderError(NativeBridge.lastError())
            return
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    /** The decoder's output size, for the copy pass's target (Task 6 calls it). */
    fun setVideoSize(w: Int, h: Int) {
        handler.post { if (handle != 0L) NativeBridge.setVideoSize(handle, w, h) }
    }

    /**
     * Stops drawing, releases the SurfaceTexture and the EGL context and surface, and ends the
     * thread. Waits (up to 2 s) for the release, so the caller's Surface is no longer drawn into
     * when it returns: call it from SurfaceHolder.Callback.surfaceDestroyed at the latest.
     */
    fun finish() {
        val done = CountDownLatch(1)
        if (handler.post { close(); done.countDown() }) {
            if (!done.await(FINISH_TIMEOUT_MS, TimeUnit.MILLISECONDS))
                Log.w(TAG, "render thread did not release within $FINISH_TIMEOUT_MS ms")
        }
        quitSafely()
    }

    private fun close() {
        running = false
        Choreographer.getInstance().removeFrameCallback(this)
        surfaceTexture?.release()
        surfaceTexture = null
        if (handle != 0L) NativeBridge.destroy(handle)
        handle = 0L
    }

    private companion object {
        const val TAG = "xrdome"
        const val FINISH_TIMEOUT_MS = 2000L
    }
}
