// Mock PDFium that models library/document/page lifetime as REAL heap objects
// so AddressSanitizer flags the wrapper's lifetime bugs as genuine memory
// errors. The mock deliberately mirrors only the documented contracts of the
// FPDF_* functions native-lib.cpp calls -- it does not "add" bugs; the bugs
// are entirely in the unmodified native-lib.cpp / PdfBridge logic.
#include <fpdfview.h>
#include <fpdf_annot.h>
#include <fpdf_doc.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>

// ---- Library global state (a single heap object, like PDFium's globals) ----
struct LibState { uint64_t magic; };
static LibState* g_lib = nullptr;
int g_initCalls = 0, g_destroyCalls = 0;

// A document captures the library instance it was created under, exactly as a
// real FPDF_DOCUMENT holds pointers into PDFium's global allocator/caches.
struct Doc {
    LibState* lib;
    int pages;
};

struct Page { int nlinks; };
struct Annot { int idx; };

static unsigned long g_lastError = 0;

// "Touching" the document exercises its link to global library state. If the
// library was destroyed while this doc was alive, this is a real
// heap-use-after-free that ASan reports -- precisely finding F1.
static void touch_lib(Doc* d, const char* where) {
    volatile uint64_t m = d->lib->magic;   // UAF read if g_lib was deleted
    if (m != 0xC0FFEEULL) abort();
}

extern "C" {

void FPDF_InitLibrary() {
    g_initCalls++;
    g_lib = new LibState{0xC0FFEEULL};
    fprintf(stderr, "[mockpdfium] FPDF_InitLibrary  (#%d) lib=%p\n", g_initCalls, (void*)g_lib);
}

void FPDF_DestroyLibrary() {
    g_destroyCalls++;
    fprintf(stderr, "[mockpdfium] FPDF_DestroyLibrary(#%d) frees lib=%p\n", g_destroyCalls, (void*)g_lib);
    delete g_lib;            // frees the global state
    g_lib = nullptr;
}

FPDF_DOCUMENT FPDF_LoadDocument(FPDF_STRING path, FPDF_BYTESTRING pw) {
    (void)pw;
    if (!g_lib) { g_lastError = 3; return nullptr; }
    Doc* d = new Doc{g_lib, 3};
    fprintf(stderr, "[mockpdfium] FPDF_LoadDocument('%s') -> doc=%p (lib=%p)\n",
            path ? path : "(null)", (void*)d, (void*)g_lib);
    return reinterpret_cast<FPDF_DOCUMENT>(d);
}

void FPDF_CloseDocument(FPDF_DOCUMENT document) {
    Doc* d = reinterpret_cast<Doc*>(document);
    fprintf(stderr, "[mockpdfium] FPDF_CloseDocument(doc=%p)\n", (void*)d);
    touch_lib(d, "FPDF_CloseDocument");  // PDFium frees objects via global allocator
    delete d;                            // double close here => ASan double-free
}

unsigned long FPDF_GetLastError() { return g_lastError; }

int FPDF_GetPageCount(FPDF_DOCUMENT document) {
    Doc* d = reinterpret_cast<Doc*>(document);
    touch_lib(d, "FPDF_GetPageCount");   // UAF if library destroyed under us
    return d->pages;
}

FPDF_PAGE FPDF_LoadPage(FPDF_DOCUMENT document, int page_index) {
    Doc* d = reinterpret_cast<Doc*>(document);
    touch_lib(d, "FPDF_LoadPage");
    if (page_index < 0 || page_index >= d->pages) return nullptr;
    const char* e = getenv("POC_NLINKS");
    return reinterpret_cast<FPDF_PAGE>(new Page{ e ? atoi(e) : 0 });
}

void FPDF_ClosePage(FPDF_PAGE page) { delete reinterpret_cast<Page*>(page); }

int FPDF_GetPageSizeByIndex(FPDF_DOCUMENT document, int, double* w, double* h) {
    Doc* d = reinterpret_cast<Doc*>(document);
    touch_lib(d, "FPDF_GetPageSizeByIndex");
    if (w) *w = 612.0; if (h) *h = 792.0;
    return 1;
}

void FPDF_RenderPageBitmap(FPDF_BITMAP, FPDF_PAGE, int, int, int, int, int, int) {}

// ---- bitmap ----
FPDF_BITMAP FPDFBitmap_CreateEx(int, int, int, void*, int) {
    return reinterpret_cast<FPDF_BITMAP>(new int(1));
}
FPDF_BOOL FPDFBitmap_FillRect(FPDF_BITMAP, int, int, int, int, FPDF_DWORD) { return 1; }
void FPDFBitmap_Destroy(FPDF_BITMAP b) { delete reinterpret_cast<int*>(b); }

// ---- annotations / links (drives nativeGetLinksForPage) ----
int FPDFPage_GetAnnotCount(FPDF_PAGE page) {
    return reinterpret_cast<Page*>(page)->nlinks;
}
FPDF_ANNOTATION FPDFPage_GetAnnot(FPDF_PAGE, int index) {
    return reinterpret_cast<FPDF_ANNOTATION>(new Annot{index});
}
void FPDFPage_CloseAnnot(FPDF_ANNOTATION a) { delete reinterpret_cast<Annot*>(a); }

FPDF_ANNOTATION_SUBTYPE FPDFAnnot_GetSubtype(FPDF_ANNOTATION) {
    return (FPDF_ANNOTATION_SUBTYPE)FPDF_ANNOT_LINK;
}
FPDF_BOOL FPDFAnnot_GetRect(FPDF_ANNOTATION, FS_RECTF* r) {
    if (r) { r->left = 1; r->top = 2; r->right = 3; r->bottom = 4; }
    return 1;
}
FPDF_LINK FPDFAnnot_GetLink(FPDF_ANNOTATION) {
    return reinterpret_cast<FPDF_LINK>(0x1);   // non-null sentinel
}
FPDF_ACTION FPDFLink_GetAction(FPDF_LINK) {
    return reinterpret_cast<FPDF_ACTION>(0x2); // non-null => URI branch
}
unsigned long FPDFAction_GetURIPath(FPDF_DOCUMENT, FPDF_ACTION, void* buf, unsigned long buflen) {
    static const char* uri = "https://example.com/poc";
    unsigned long need = (unsigned long)strlen(uri) + 1;
    if (buf && buflen >= need) memcpy(buf, uri, need);
    return need;                               // drives NewStringUTF leak branch
}
FPDF_DEST FPDFLink_GetDest(FPDF_DOCUMENT, FPDF_LINK) { return nullptr; }
int FPDFDest_GetDestPageIndex(FPDF_DOCUMENT, FPDF_DEST) { return -1; }

} // extern "C"
