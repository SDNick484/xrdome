package io.github.sdnick484.xrdome.play

import android.app.Presentation
import android.content.Context
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Display
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager

/**
 * The glasses' screen (spec 5.3 Display): a [Presentation] on the glasses' display that asks for
 * mode [modeId] (3840x1080 at 60 Hz, from [DisplayPicker]) and runs a [RenderThread] on its
 * SurfaceView's surface while the surface exists and is 3840x1080. A new surface size stops the
 * thread (and starts a new one if the size is 3840x1080 again).
 * [events] are called on the main thread.
 */
class GlassesPresentation(
    outerContext: Context,
    display: Display,
    private val modeId: Int,
    private val fovDiag: Float,
    private val pattern: Boolean,
    private val events: Events,
) : Presentation(outerContext, display), SurfaceHolder.Callback {

    interface Events {
        /** The render thread's SurfaceTexture (for the decoder, Task 6). */
        fun onSurfaceTexture(surfaceTexture: SurfaceTexture) {}

        /** The render thread could not make its context or lost its surface. */
        fun onRenderError(message: String)
    }

    private val main = Handler(Looper.getMainLooper())
    private var render: RenderThread? = null

    /** The render thread now drawing, if any (Task 6 gives it the video size). */
    val renderThread: RenderThread? get() = render

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window?.let { w ->
            w.attributes = w.attributes.also { it.preferredDisplayModeId = modeId }
            w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        val view = SurfaceView(context)
        view.holder.addCallback(this)
        setContentView(view)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {}

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        val r = render
        if (r != null && r.width == width && r.height == height) return
        stopRender()
        // Only the SBS size is drawn: a surface still at 1920x1080 (the mode switch not done, or
        // refused) stays black rather than showing both eyes squashed into it.
        if (width != DisplayPicker.SBS_W || height != DisplayPicker.SBS_H) return
        val listener = object : RenderThread.Listener {
            override fun onSurfaceTexture(surfaceTexture: SurfaceTexture) {
                main.post { events.onSurfaceTexture(surfaceTexture) }
            }

            override fun onRenderError(message: String) {
                main.post { events.onRenderError(message) }
            }
        }
        render = RenderThread(holder.surface, width, height, fovDiag, listener).also {
            it.begin(pattern)
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        stopRender()
    }

    override fun onStop() {
        stopRender()
        super.onStop()
    }

    private fun stopRender() {
        render?.finish()
        render = null
    }
}
