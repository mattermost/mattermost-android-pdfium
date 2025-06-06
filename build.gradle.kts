plugins {
    id("com.android.library")
    kotlin("android")
    id("maven-publish")
}

group = "com.mattermost"
version = "1.0.0"

android {
    namespace = "com.mattermost.pdfium"
    compileSdk = 34

    defaultConfig {
        minSdk = 24

        // Native build setup
        externalNativeBuild {
            cmake {
                cppFlags +="-std=c++17"
            }
        }

        ndk {
            abiFilters += listOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
        }

        version = "1.0.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            consumerProguardFiles("consumer-rules.pro")
        }
    }

    // Path to .so libs per ABI
    sourceSets["main"].jniLibs.srcDirs("src/main/jniLibs")

    // Path to CMake file
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        buildConfig = false
    }
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation(kotlin("stdlib"))
}

afterEvaluate {
    publishing {
        publications {
            create<MavenPublication>("release") {
                from(components["release"])
                groupId = group.toString()
                artifactId = "mattermost-android-pdfium"
                version = version.toString()
            }
        }
    }
}
