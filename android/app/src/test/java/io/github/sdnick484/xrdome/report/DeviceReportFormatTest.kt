package io.github.sdnick484.xrdome.report

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DeviceReportFormatTest {
    private fun sample() = DeviceReport(
        model = "Google Pixel 9 Pro XL (komodo)",
        androidVersion = "15 (SDK 35)",
        soc = "Google Tensor G4",
        decoders = listOf(
            DecoderEntry("video/avc", "c2.exynos.h264.decoder", true, true, true, false),
            DecoderEntry("video/av01", "c2.android.av1.decoder", false, false, false, false),
        ),
        displays = listOf(
            DisplayEntry(0, "Built-in", 0x3, false, listOf("1344x2992@120.00")),
            DisplayEntry(2, "VITURE", 0x8, true, listOf("3840x1080@60.00", "1920x1080@60.00")),
        ),
        gl = "GL_RENDERER: Mali-G715\nGL_OES_EGL_image_external_essl3: yes\n",
        usb = listOf(UsbEntry(0x35ca, 0x101d, "VITURE Pro"), UsbEntry(0x1, 0xab, null)),
    )

    @Test
    fun sectionsAppearInOrder() {
        val text = sample().toText()
        val heads = listOf("[build]", "[decoders]", "[displays]", "[gl]", "[usb]")
        assertEquals(heads, text.lines().filter { it.startsWith("[") })
    }

    @Test
    fun ratesRenderAsYesNo() {
        val lines = sample().toText().lines()
        val h264 = lines.first { it.startsWith("video/avc") }
        assertTrue(h264, "hardware size4320x2160=yes rate30=yes rate60=no" in h264)
        val av1 = lines.first { it.startsWith("video/av01") }
        assertTrue(av1, "software size4320x2160=no rate30=no rate60=no" in av1)
    }

    @Test
    fun displaysModesGlAndUsbAreListed() {
        val text = sample().toText()
        assertTrue("  mode 3840x1080@60.00" in text)
        assertTrue("presentation=yes" in text && "presentation=no" in text)
        assertTrue("GL_OES_EGL_image_external_essl3: yes" in text)
        assertTrue("35ca:101d VITURE Pro" in text)
        assertTrue("0001:00ab (no product name)" in text)
    }
}
