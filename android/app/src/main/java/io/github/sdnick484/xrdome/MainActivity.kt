package io.github.sdnick484.xrdome

import android.app.Activity
import android.content.Intent
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import io.github.sdnick484.xrdome.play.DisplayPicker
import io.github.sdnick484.xrdome.play.GlassesPresentation
import io.github.sdnick484.xrdome.play.Pick
import io.github.sdnick484.xrdome.report.ReportActivity

class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var patternButton: Button
    private lateinit var displayManager: DisplayManager

    /** The test pattern on the glasses, while it is shown. */
    private var shown: GlassesPresentation? = null
    private var shownDisplayId = -1

    /** The glasses' display has been seen in the 3840x1080 mode since the pattern went up. */
    private var reachedSbs = false

    private val main = Handler(Looper.getMainLooper())

    /** The Presentation asked for the SBS mode; if the display is not in it by now, say so. */
    private val sbsDeadline = Runnable {
        if (shown != null && !reachedSbs) {
            val m = displayManager.getDisplay(shownDisplayId)?.let { DisplayPicker.mode(it.mode) }
            stopPattern(notSbsMessage(m?.w ?: 0, m?.h ?: 0))
        }
    }

    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) {}

        override fun onDisplayRemoved(displayId: Int) {
            if (shown != null && displayId == shownDisplayId) stopPattern(GLASSES_DISCONNECTED)
        }

        override fun onDisplayChanged(displayId: Int) {
            if (shown == null || displayId != shownDisplayId) return
            val d = displayManager.getDisplay(displayId)
            if (d == null) {
                stopPattern(GLASSES_DISCONNECTED)
                return
            }
            val m = DisplayPicker.mode(d.mode)
            if (DisplayPicker.isSbs(m)) {
                reachedSbs = true
            } else if (reachedSbs) {
                // Left SBS after being in it (the glasses' own button, or a reconnect). Before
                // that, a change is the mode switch the Presentation asked for still on its way.
                stopPattern(notSbsMessage(m.w, m.h))
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        displayManager = getSystemService(DisplayManager::class.java)
        val title = TextView(this).apply {
            text = "xrdome ${NativeBridge.version()}"
            textSize = 24f
            gravity = Gravity.CENTER
        }
        patternButton = Button(this).apply {
            text = SHOW_PATTERN
            setOnClickListener { if (shown == null) showPattern() else stopPattern(null) }
        }
        val report = Button(this).apply {
            text = "Device report"
            setOnClickListener { startActivity(Intent(this@MainActivity, ReportActivity::class.java)) }
        }
        status = TextView(this).apply {
            gravity = Gravity.CENTER
            setPadding(24, 24, 24, 24)
        }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            addView(title)
            addView(patternButton)
            addView(report)
            addView(status)
        })
    }

    override fun onStart() {
        super.onStart()
        displayManager.registerDisplayListener(displayListener, main)
    }

    /** Leaving the foreground takes the pattern down: the Presentation, EGL and the thread go. */
    override fun onStop() {
        stopPattern(null)
        displayManager.unregisterDisplayListener(displayListener)
        super.onStop()
    }

    private fun showPattern() {
        when (val pick = DisplayPicker.pick(DisplayPicker.displays(displayManager))) {
            Pick.NoExternal -> status.text = "Connect the glasses"
            is Pick.NotSbs -> status.text = notSbsMessage(pick.currentW, pick.currentH)
            is Pick.Ok -> showOn(pick)
        }
    }

    private fun showOn(pick: Pick.Ok) {
        val display = displayManager.getDisplay(pick.displayId)
        if (display == null) {
            status.text = "Connect the glasses"
            return
        }
        val fov = NativeBridge.deviceFov(DEVICE)
        val events = object : GlassesPresentation.Events {
            override fun onRenderError(message: String) {
                stopPattern("The glasses cannot show the picture: $message")
            }
        }
        val p = GlassesPresentation(this, display, pick.modeId, fov, pattern = true, events)
        try {
            p.show()
        } catch (e: WindowManager.InvalidDisplayException) {
            status.text = GLASSES_DISCONNECTED
            return
        }
        shown = p
        shownDisplayId = pick.displayId
        reachedSbs = DisplayPicker.isSbs(DisplayPicker.mode(display.mode))
        if (!reachedSbs) main.postDelayed(sbsDeadline, SBS_DEADLINE_MS)
        patternButton.text = STOP_PATTERN
        status.text = "Test pattern on the glasses: four colours in each eye, no seam in the middle."
    }

    /** Dismisses the pattern (the Presentation's onStop releases EGL) and shows [message]. */
    private fun stopPattern(message: String?) {
        val p = shown ?: return
        main.removeCallbacks(sbsDeadline)
        shown = null
        shownDisplayId = -1
        p.dismiss()
        patternButton.text = SHOW_PATTERN
        status.text = message ?: ""
    }

    private fun notSbsMessage(w: Int, h: Int) =
        "Switch the glasses to 3D (SBS) mode: they are at ${w}x$h"

    private companion object {
        /** The glasses this task supports; the XREAL comes with Task 11. */
        const val DEVICE = "viture-pro"
        const val SHOW_PATTERN = "Show test pattern"
        const val STOP_PATTERN = "Stop test pattern"
        const val GLASSES_DISCONNECTED = "Glasses disconnected"
        const val SBS_DEADLINE_MS = 3000L
    }
}
