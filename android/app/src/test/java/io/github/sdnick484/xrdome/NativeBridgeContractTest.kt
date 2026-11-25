package io.github.sdnick484.xrdome

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test

class NativeBridgeContractTest {
    /** The app's version is the one in meson.build's project(version:). */
    @Test
    fun versionMatchesMeson() {
        val path = System.getProperty("xrdome.mesonBuild")
        assertNotNull("xrdome.mesonBuild is set by app/build.gradle.kts", path)
        val meson = File(path!!).readText()
        val m = Regex("""project\(\s*'xrdome'.*?version\s*:\s*'([^']+)'""", RegexOption.DOT_MATCHES_ALL)
            .find(meson)
        assertNotNull("project(version:) in meson.build", m)
        assertEquals(m!!.groupValues[1], BuildConfig.XRD_VERSION)
    }
}
