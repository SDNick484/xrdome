import java.io.File

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// The repository root (android/ is one level below it) and the version in meson.build's
// project(version:), so the app and the Linux player report the same one.
val repoRoot: File = rootProject.projectDir.parentFile
val xrdVersion: String = Regex("""(?m)^\s*version\s*:\s*'([^']+)'""")
    .find(File(repoRoot, "meson.build").readText())?.groupValues?.get(1)
    ?: throw GradleException("no project(version:) in meson.build")

android {
    namespace = "io.github.sdnick484.xrdome"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.github.sdnick484.xrdome"
        minSdk = 33
        targetSdk = 35
        versionCode = 1
        versionName = xrdVersion
        buildConfigField("String", "XRD_VERSION", "\"$xrdVersion\"")
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake { arguments += listOf("-DANDROID_STL=none") }
        }
    }

    buildFeatures { buildConfig = true }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // Debug-signed only (the SDK's debug keystore); no release variant is built.
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    testOptions {
        unitTests.all {
            // NativeBridgeContractTest reads meson.build from the repository.
            it.systemProperty("xrdome.mesonBuild", File(repoRoot, "meson.build").absolutePath)
        }
    }

    lint {
        abortOnError = true
        warningsAsErrors = false
    }
}

kotlin { jvmToolchain(17) }

dependencies {
    testImplementation("junit:junit:4.13.2")
}
