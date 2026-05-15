// Minimal host stub of <android/bitmap.h> so the UNMODIFIED native-lib.cpp
// compiles and runs off-device. Layout/return codes match the NDK contract.
#pragma once
#include <jni.h>
#include <cstdint>

#define ANDROID_BITMAP_RESULT_SUCCESS 0
#define ANDROID_BITMAP_RESULT_BAD_PARAMETER -1

enum AndroidBitmapFormat {
    ANDROID_BITMAP_FORMAT_NONE = 0,
    ANDROID_BITMAP_FORMAT_RGBA_8888 = 1,
    ANDROID_BITMAP_FORMAT_RGB_565 = 4,
    ANDROID_BITMAP_FORMAT_A_8 = 8,
};

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int32_t  format;
    uint32_t flags;
} AndroidBitmapInfo;

int AndroidBitmap_getInfo(JNIEnv* env, jobject bitmap, AndroidBitmapInfo* info);
int AndroidBitmap_lockPixels(JNIEnv* env, jobject bitmap, void** pixels);
int AndroidBitmap_unlockPixels(JNIEnv* env, jobject bitmap);
