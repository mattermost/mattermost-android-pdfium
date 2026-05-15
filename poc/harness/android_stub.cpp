// Stub the 3 AndroidBitmap_* symbols so the renderer path links (not exercised
// by the lifetime scenarios, but native-lib.cpp references them).
#include <android/bitmap.h>
int AndroidBitmap_getInfo(JNIEnv*, jobject, AndroidBitmapInfo* i) {
    if (i) { i->width = 8; i->height = 8; i->stride = 32;
             i->format = ANDROID_BITMAP_FORMAT_RGBA_8888; i->flags = 0; }
    return ANDROID_BITMAP_RESULT_SUCCESS;
}
static unsigned char g_px[8 * 32];
int AndroidBitmap_lockPixels(JNIEnv*, jobject, void** p) { if (p) *p = g_px; return 0; }
int AndroidBitmap_unlockPixels(JNIEnv*, jobject) { return 0; }
