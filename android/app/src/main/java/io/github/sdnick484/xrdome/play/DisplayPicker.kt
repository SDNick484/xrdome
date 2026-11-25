package io.github.sdnick484.xrdome.play

import android.hardware.display.DisplayManager
import android.view.Display
import kotlin.math.abs

/** One display mode: [w] x [h] pixels at [hz]; [id] is Display.Mode.getModeId(). */
data class Mode(val w: Int, val h: Int, val hz: Float, val id: Int = 0)

/** What [DisplayPicker] needs of a display, so it is testable without a device. */
data class DisplayInfo(
    val id: Int,
    val isPresentation: Boolean,
    val modes: List<Mode>,
    val currentMode: Mode?,
)

/** The outcome of [DisplayPicker.pick]. */
sealed interface Pick {
    /** Show on [displayId] in mode [modeId] (3840x1080 at about 60 Hz). */
    data class Ok(val displayId: Int, val modeId: Int) : Pick

    /** No presentation display: the glasses are not connected (or not seen as a display). */
    data object NoExternal : Pick

    /** The glasses are there but offer no 3840x1080 mode: they are in 2D (1920x1080) mode. */
    data class NotSbs(val currentW: Int, val currentH: Int) : Pick
}

/**
 * Finds the glasses among the displays (spec 5.3 Display): a presentation display with a mode of
 * exactly 3840x1080 (both eyes side by side) at 59-61 Hz.
 */
object DisplayPicker {
    const val SBS_W = 3840
    const val SBS_H = 1080
    private const val HZ_MIN = 59f
    private const val HZ_MAX = 61f

    fun isSbs(m: Mode): Boolean = m.w == SBS_W && m.h == SBS_H && m.hz in HZ_MIN..HZ_MAX

    fun pick(displays: List<DisplayInfo>): Pick {
        val external = displays.filter { it.isPresentation }
        if (external.isEmpty()) return Pick.NoExternal
        for (d in external) {
            // Among several SBS modes, the one nearest 60 Hz.
            val mode = d.modes.filter(::isSbs).minByOrNull { abs(it.hz - 60f) }
            if (mode != null) return Pick.Ok(d.id, mode.id)
        }
        val first = external.first()
        val cur = first.currentMode ?: first.modes.firstOrNull()
        return Pick.NotSbs(cur?.w ?: 0, cur?.h ?: 0)
    }

    /** The phone's displays as [DisplayInfo]; presentation means in DISPLAY_CATEGORY_PRESENTATION. */
    fun displays(dm: DisplayManager): List<DisplayInfo> {
        val presentation = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .map { it.displayId }.toSet()
        return dm.displays.map { info(it, it.displayId in presentation) }
    }

    fun info(d: Display, isPresentation: Boolean): DisplayInfo = DisplayInfo(
        id = d.displayId,
        isPresentation = isPresentation,
        modes = d.supportedModes.map(::mode),
        currentMode = mode(d.mode),
    )

    fun mode(m: Display.Mode): Mode =
        Mode(m.physicalWidth, m.physicalHeight, m.refreshRate, m.modeId)
}
