package io.github.sdnick484.xrdome.play

import org.junit.Assert.assertEquals
import org.junit.Test

class DisplayPickerTest {
    private val phone = DisplayInfo(
        id = 0,
        isPresentation = false,
        modes = listOf(Mode(1344, 2992, 120f, 1), Mode(1344, 2992, 60f, 2)),
        currentMode = Mode(1344, 2992, 120f, 1),
    )

    @Test
    fun picksSbsMode() {
        val glasses = DisplayInfo(
            id = 2,
            isPresentation = true,
            modes = listOf(
                Mode(1920, 1080, 60f, 10),
                Mode(3840, 1080, 59.94f, 11),
                Mode(3840, 1080, 72f, 12),
                Mode(3840, 1080, 60f, 13),
            ),
            currentMode = Mode(1920, 1080, 60f, 10),
        )
        assertEquals(Pick.Ok(2, 13), DisplayPicker.pick(listOf(phone, glasses)))
    }

    @Test
    fun rejects1080p() {
        val glasses = DisplayInfo(
            id = 2,
            isPresentation = true,
            modes = listOf(Mode(1920, 1080, 60f, 10)),
            currentMode = Mode(1920, 1080, 60f, 10),
        )
        assertEquals(Pick.NotSbs(1920, 1080), DisplayPicker.pick(listOf(phone, glasses)))
    }

    @Test
    fun noExternal() {
        assertEquals(Pick.NoExternal, DisplayPicker.pick(emptyList()))
    }

    @Test
    fun ignoresNonPresentation() {
        // A non-presentation display with a 3840x1080 mode is not the glasses.
        val other = DisplayInfo(
            id = 3,
            isPresentation = false,
            modes = listOf(Mode(3840, 1080, 60f, 20)),
            currentMode = Mode(3840, 1080, 60f, 20),
        )
        assertEquals(Pick.NoExternal, DisplayPicker.pick(listOf(phone, other)))
    }
}
