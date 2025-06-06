# mattermost-android-pdfium

**PDFium JNI wrapper for Android**, packaged as a standalone AAR library for use in Android applications. This library provides native access to PDFium’s core rendering features including:

- Secure PDF document loading with password support
- High-performance bitmap rendering
- Page count and page size extraction
- PDF link (URI and internal navigation) mapping
- Graceful error handling and resource cleanup

## Usage

This library is designed to be consumed via JitPack or MavenLocal. Add the following to your root `build.gradle`:

```groovy
allprojects {
    repositories {
        maven { url 'https://jitpack.io' }
    }
}

implementation 'com.github.mattermost:mattermost-android-pdfium:1.0.0'
```

## Setup

To download and install PDFium binaries:

```bash
./scripts/setup-pdfium.sh <version>
```

You can check for available updates with:
```
./scripts/setup-pdfium.sh --check-updates
```