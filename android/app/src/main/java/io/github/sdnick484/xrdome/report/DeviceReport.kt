package io.github.sdnick484.xrdome.report

import android.content.Context
import android.hardware.display.DisplayManager
import android.hardware.usb.UsbManager
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.os.Build
import io.github.sdnick484.xrdome.NativeBridge
import java.util.Locale

/** One decoder for one MIME type and what it says about 4320x2160 (hardware check H1). */
data class DecoderEntry(
    val mime: String,
    val name: String,
    val hardware: Boolean,
    val size4320x2160: Boolean,
    val rate30: Boolean,
    val rate60: Boolean,
)

/** One display (hardware checks H2, H3); [modes] are already "WxH@Hz" strings. */
data class DisplayEntry(
    val id: Int,
    val name: String,
    val flags: Int,
    val presentation: Boolean,
    val modes: List<String>,
)

/** One attached USB device (hardware check H5): ids and name only, no serial number. */
data class UsbEntry(val vendorId: Int, val productId: Int, val productName: String?)

/**
 * What the owner needs to run the hardware checks without tools on the phone (spec 5.3).
 * [toText] is pure, so the format is unit-tested; [collect] reads the device.
 */
data class DeviceReport(
    val model: String,
    val androidVersion: String,
    val soc: String,
    val decoders: List<DecoderEntry>,
    val displays: List<DisplayEntry>,
    /** The lines of NativeBridge.glInfo(), as they came. */
    val gl: String,
    val usb: List<UsbEntry>,
) {
    /** The sections in this order: [build], [decoders], [displays], [gl], [usb]. */
    fun toText(): String = buildString {
        appendLine("[build]")
        appendLine("model: $model")
        appendLine("android: $androidVersion")
        appendLine("soc: $soc")

        appendLine()
        appendLine("[decoders]")
        if (decoders.isEmpty()) appendLine("(none)")
        for (d in decoders) {
            appendLine(
                "${d.mime} ${d.name} ${if (d.hardware) "hardware" else "software"}" +
                    " size4320x2160=${yn(d.size4320x2160)}" +
                    " rate30=${yn(d.rate30)} rate60=${yn(d.rate60)}"
            )
        }

        appendLine()
        appendLine("[displays]")
        if (displays.isEmpty()) appendLine("(none)")
        for (d in displays) {
            appendLine(
                "display ${d.id} \"${d.name}\" flags=0x${Integer.toHexString(d.flags)}" +
                    " presentation=${yn(d.presentation)}"
            )
            for (m in d.modes) appendLine("  mode $m")
        }

        appendLine()
        appendLine("[gl]")
        append(gl.trimEnd('\n')).appendLine()

        appendLine()
        appendLine("[usb]")
        if (usb.isEmpty()) appendLine("(none)")
        for (u in usb) {
            appendLine(
                String.format(Locale.ROOT, "%04x:%04x", u.vendorId, u.productId) +
                    " ${u.productName ?: "(no product name)"}"
            )
        }
    }

    companion object {
        /** The three formats H1 asks about. */
        val MIMES = listOf("video/avc", "video/hevc", "video/av01")

        private fun yn(b: Boolean) = if (b) "yes" else "no"

        /** Reads the device. Call on a thread where creating a throwaway EGL context is fine. */
        fun collect(context: Context): DeviceReport = DeviceReport(
            model = "${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE})",
            androidVersion = "${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})",
            soc = "${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}",
            decoders = collectDecoders(),
            displays = collectDisplays(context),
            gl = NativeBridge.glInfo(),
            usb = collectUsb(context),
        )

        private fun collectDecoders(): List<DecoderEntry> {
            val out = ArrayList<DecoderEntry>()
            val infos = MediaCodecList(MediaCodecList.ALL_CODECS).codecInfos
            for (mime in MIMES) {
                for (info in infos) {
                    if (info.isEncoder) continue
                    if (info.supportedTypes.none { it.equals(mime, ignoreCase = true) }) continue
                    out += decoderEntry(mime, info)
                }
            }
            return out
        }

        private fun decoderEntry(mime: String, info: MediaCodecInfo): DecoderEntry {
            val v = try {
                info.getCapabilitiesForType(mime).videoCapabilities
            } catch (e: IllegalArgumentException) {
                null
            }
            return DecoderEntry(
                mime = mime,
                name = info.name,
                hardware = info.isHardwareAccelerated,
                size4320x2160 = v?.isSizeSupported(4320, 2160) ?: false,
                rate30 = v?.areSizeAndRateSupported(4320, 2160, 30.0) ?: false,
                rate60 = v?.areSizeAndRateSupported(4320, 2160, 60.0) ?: false,
            )
        }

        private fun collectDisplays(context: Context): List<DisplayEntry> {
            val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
            val presentationIds = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
                .map { it.displayId }.toSet()
            return dm.displays.map { d ->
                DisplayEntry(
                    id = d.displayId,
                    name = d.name,
                    flags = d.flags,
                    presentation = d.displayId in presentationIds,
                    modes = d.supportedModes.map {
                        String.format(
                            Locale.ROOT, "%dx%d@%.2f",
                            it.physicalWidth, it.physicalHeight, it.refreshRate,
                        )
                    },
                )
            }
        }

        private fun collectUsb(context: Context): List<UsbEntry> {
            val um = context.getSystemService(Context.USB_SERVICE) as UsbManager
            return um.deviceList.values.map {
                UsbEntry(it.vendorId, it.productId, it.productName)
            }
        }
    }
}
