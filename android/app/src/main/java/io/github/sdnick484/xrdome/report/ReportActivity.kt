package io.github.sdnick484.xrdome.report

import android.app.Activity
import android.graphics.Typeface
import android.os.Bundle
import android.widget.ScrollView
import android.widget.TextView
import java.io.File
import java.io.IOException

/**
 * Shows the device report and writes it to getExternalFilesDir(null)/device-report.txt, so the
 * owner can `adb pull` it (docs/android.md).
 */
class ReportActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val report = DeviceReport.collect(this).toText()
        val note = try {
            val dir = getExternalFilesDir(null) ?: throw IOException("no external files directory")
            val file = File(dir, FILE_NAME)
            file.writeText(report)
            "Written to ${file.absolutePath}\n\n"
        } catch (e: IOException) {
            "Could not write $FILE_NAME: ${e.message}\n\n"
        }
        val text = TextView(this).apply {
            this.text = note + report
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setTextIsSelectable(true)
            setPadding(24, 24, 24, 24)
        }
        setContentView(ScrollView(this).apply { addView(text) })
    }

    companion object {
        const val FILE_NAME = "device-report.txt"
    }
}
