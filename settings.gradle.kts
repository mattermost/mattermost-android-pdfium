pluginManagement {
    repositories {
        google()               // Needed for com.android.* plugins
        gradlePluginPortal()   // Needed for kotlin("android")
        mavenCentral()
    }

    plugins {
        id("com.android.library") version "8.4.2"
        id("org.jetbrains.kotlin.android") version "1.9.25"
    }
}

// @Incubating — still OK to use
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "mattermost-android-pdfium"
