// Mock JNIEnv. Implements only the JNI slots native-lib.cpp uses, and
// accounts local references exactly the way ART's IndirectReferenceTable
// does: every FindClass / NewObject / NewStringUTF / New*Array creates one
// local ref; DeleteLocalRef removes one. This lets us measure the real
// per-annotation local-ref leak in nativeGetLinksForPage (finding F4).
#include <jni.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <atomic>

static std::atomic<long> g_liveRefs{0};
static std::atomic<long> g_totalRefs{0};
static long g_peakRefs = 0;
static bool g_pending = false;

long jnimock_live_refs()  { return g_liveRefs.load(); }
long jnimock_total_refs() { return g_totalRefs.load(); }
long jnimock_peak_refs()  { return g_peakRefs; }
void jnimock_reset()      { g_liveRefs = 0; g_totalRefs = 0; g_peakRefs = 0; g_pending = false; }

static jobject newref(const char* what) {
    long n = ++g_liveRefs;
    ++g_totalRefs;
    if (n > g_peakRefs) g_peakRefs = n;
    (void)what;
    return reinterpret_cast<jobject>(static_cast<intptr_t>(g_totalRefs.load()) | 0x40000000);
}

static jclass    jni_FindClass(JNIEnv*, const char*)                 { return (jclass)newref("class"); }
static jmethodID jni_GetMethodID(JNIEnv*, jclass, const char*, const char*) { return (jmethodID)0x1234; }
static jint      jni_ThrowNew(JNIEnv*, jclass, const char* m) {
    fprintf(stderr, "[mockjni] ThrowNew: %s\n", m ? m : "");
    g_pending = true; return 0;
}
static jboolean  jni_ExceptionCheck(JNIEnv*) { return g_pending ? JNI_TRUE : JNI_FALSE; }
static void      jni_ExceptionClear(JNIEnv*) { g_pending = false; }

static jobject   jni_NewObjectV(JNIEnv*, jclass, jmethodID, va_list)  { return newref("obj"); }
static jstring   jni_NewStringUTF(JNIEnv*, const char*)               { return (jstring)newref("str"); }
static jobjectArray jni_NewObjectArray(JNIEnv*, jsize, jclass, jobject){ return (jobjectArray)newref("arr"); }
static void      jni_SetObjectArrayElement(JNIEnv*, jobjectArray, jsize, jobject) {}
static jfloatArray jni_NewFloatArray(JNIEnv*, jsize)                  { return (jfloatArray)newref("farr"); }
static void      jni_SetFloatArrayRegion(JNIEnv*, jfloatArray, jsize, jsize, const jfloat*) {}

static void      jni_DeleteLocalRef(JNIEnv*, jobject o) {
    if (o) --g_liveRefs;   // ART frees one slot
}

// jstring passed in from the harness is backed by a std::string*.
static const char* jni_GetStringUTFChars(JNIEnv*, jstring s, jboolean* iscopy) {
    if (iscopy) *iscopy = JNI_FALSE;
    return s ? reinterpret_cast<std::string*>(s)->c_str() : nullptr;
}
static void jni_ReleaseStringUTFChars(JNIEnv*, jstring, const char*) {}

static JNINativeInterface_ g_tbl;
static JNIEnv_ g_env;

JNIEnv* make_mock_env() {
    memset(&g_tbl, 0, sizeof(g_tbl));
    g_tbl.FindClass              = jni_FindClass;
    g_tbl.GetMethodID            = jni_GetMethodID;
    g_tbl.ThrowNew               = jni_ThrowNew;
    g_tbl.ExceptionCheck         = jni_ExceptionCheck;
    g_tbl.ExceptionClear         = jni_ExceptionClear;
    g_tbl.NewObjectV             = jni_NewObjectV;
    g_tbl.NewStringUTF           = jni_NewStringUTF;
    g_tbl.NewObjectArray         = jni_NewObjectArray;
    g_tbl.SetObjectArrayElement  = jni_SetObjectArrayElement;
    g_tbl.NewFloatArray          = jni_NewFloatArray;
    g_tbl.SetFloatArrayRegion    = jni_SetFloatArrayRegion;
    g_tbl.DeleteLocalRef         = jni_DeleteLocalRef;
    g_tbl.GetStringUTFChars      = jni_GetStringUTFChars;
    g_tbl.ReleaseStringUTFChars  = jni_ReleaseStringUTFChars;
    g_env.functions = &g_tbl;
    return reinterpret_cast<JNIEnv*>(&g_env);
}
