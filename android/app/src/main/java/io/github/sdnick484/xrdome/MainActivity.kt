package io.github.sdnick484.xrdome

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import io.github.sdnick484.xrdome.report.ReportActivity

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val title = TextView(this).apply {
            text = "xrdome ${NativeBridge.version()}"
            textSize = 24f
            gravity = Gravity.CENTER
        }
        val report = Button(this).apply {
            text = "Device report"
            setOnClickListener { startActivity(Intent(this@MainActivity, ReportActivity::class.java)) }
        }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            addView(title)
            addView(report)
        })
    }
}
