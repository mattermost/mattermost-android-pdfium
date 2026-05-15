# Security Review — mattermost-android-pdfium

Scope: JNI wrapper (`src/main/cpp/native-lib.cpp`, `PdfBridge.kt`), build/supply
chain, bundled PDFium binary. Threat model: the library is published (JitPack)
and used by the Mattermost Android app to open and render **untrusted** PDFs
(chat attachments). Attacker controls PDF bytes and requested page index; the
host app controls the `Bitmap` and document lifecycle.

Three independent analyses (primary + two agents) converged on the same set.
Findings below are the confirmed Critical/High issues that break at least one
of Confidentiality / Integrity / Availability with **few prerequisites**.

---

## F1 — CRITICAL: global `FPDF_DestroyLibrary()` on every document close → cross-document use-after-free

`native-lib.cpp:28-39` (`DocumentWrapper::cleanup`) + `:85-89` + `PdfBridge.kt:48-57`.
CWE-416 / CWE-672.

`cleanup()` calls `FPDF_DestroyLibrary()` and clears the process-global
`pdfium_initialized` flag **every time any single document is closed**, with no
refcount of live documents. PDFium's library state (allocator, font/codec
caches, module manager) is process-global.

**Fully deterministic, single-threaded, no race, no GC, no malicious input.**
The minimal trigger is idiomatic consumer code that preloads two attachments
and frees the first:

```kotlin
val a = PdfBridge.open("A.pdf")   // FPDF_InitLibrary(); pdfium_initialized=true
val b = PdfBridge.open("B.pdf")   // init skipped; B lives in the SAME library
a.close()                         // cleanup(): FPDF_CloseDocument(A); FPDF_DestroyLibrary()
if (b.isValid()) b.getPageCount() // FPDF_GetPageCount(B) on destroyed global state → UAF
```

Amplifier — **the natural guard is unsound**: `nativeIsDocumentValid`
(`native-lib.cpp:145-158`) only checks `wrapper && wrapper->get() != nullptr`;
it never touches library state, so after A's `FPDF_DestroyLibrary()` it still
returns `true` for B. A defensive consumer that checks `isValid()` before use
is led straight into the UAF.

Escalation (F1b, also ASan-proven): open PDF C after `a.close()` → flag is
false → `FPDF_InitLibrary()` re-entered **while B is still live**; a subsequent
`b.renderPageToBitmap(...)` parses attacker-influenced content through a
freshly re-initialised allocator while B's objects still point into the
destroyed arenas → cross-allocator heap confusion during render.

Impact: deterministic crash (Availability) on the plain `getPageCount`/`close`
path; Integrity/Confidentiality on the render-after-reinit path (heap
confusion shaped by attacker PDF allocations — realistic disclosure/RCE
potential, though weaponisation not built). Prerequisite is only "two
documents whose lifetimes overlap" — routine in a chat client (thumbnail +
viewer, multiple attachments, list preloading). The Kotlin `finalize()` GC
path is an *additional* (non-deterministic) way in, not the only one.

Fix: never destroy the library per-document. Drop the teardown from
`cleanup()`, or refcount live wrappers under `pdfium_mutex` and only
`FPDF_DestroyLibrary()` at zero (or never, for a long-lived process).

## F2 — CRITICAL: PDFium is not thread-safe; finalizer thread races renderer with no lock

Render `:236-307`, links `:309-390`, count `:160-189`, size `:191-234`,
close `:127-141` take **no lock**; `pdfium_mutex` only guards init/destroy.
CWE-362 → CWE-416.

`finalize()` runs on ART's FinalizerDaemon thread, asynchronous to the
UI/render thread. While thread 1 is inside `FPDF_RenderPageBitmap` parsing an
attacker PDF, thread 2 can finalize another document → `FPDF_CloseDocument` +
`FPDF_DestroyLibrary()` (F1) concurrently → data race + UAF mid-render. The
attacker widens the window with large/nested content streams. Compounds F1.

Fix: serialize **all** PDFium entry points under one global mutex; never call
PDFium from a finalizer without it.

## F3 — HIGH: `nativeRenderPageToBitmap` ignores `AndroidBitmapInfo.format` → OOB heap write

`native-lib.cpp:262-300`. CWE-787 / CWE-843.

`bitmapInfo.format` is never checked. The buffer is unconditionally treated as
32-bit `FPDFBitmap_BGRx` and `swapRedBlueChannels` writes `width` `uint32_t`
per row. If the host passes an `RGB_565` (2 B/px) or `ALPHA_8` (1 B/px)
`Bitmap` — both valid, and `RGB_565` is common for memory-frugal
thumbnails — `FPDF_RenderPageBitmap`/`FPDFBitmap_FillRect` and
`swapRedBlueChannels` write `4*w*h` bytes into a `2*w*h` / `1*w*h` allocation:
large linear heap overflow. Triggered by host bitmap config (not the PDF), so
moderate prerequisite, but it is an unsafe, unvalidated API contract on a
generally-consumed library.

Fix: reject `format != ANDROID_BITMAP_FORMAT_RGBA_8888`; assert
`stride >= 4u*targetWidth` before `FPDFBitmap_CreateEx`.

## F4 — MEDIUM: JNI local-reference leak in link loop → resource-exhaustion DoS

`native-lib.cpp:329-389`. CWE-789 / CWE-401. Leak proven with PoC; severity
**corrected down** from an earlier draft — see the threshold note below.

Per Link annotation the loop creates and does **not** release: `rectFClass`
(`FindClass`), `jRectF` (`NewObject`), `NewStringUTF(uri)`, and when
`destPage>=0` two `FindClass("java/lang/Integer")` + the `Integer` object.
Only `jLink` is freed — and only *after* the whole loop. The harness measures
exactly **3.0 leaked JNI local refs per Link annotation**, and each also pins
a live Java object (`RectF`/`String`/`Integer`).

**Correction (important).** An earlier draft claimed ART aborts at ~512 local
refs (~128 annotations). That ~512 figure is **Dalvik-only (Android ≤4.4,
EOL)**. This library sets `minSdk = 24`, so every supported target is ART.
Modern ART (`art/runtime/jni/local_reference_table.*`,
`indirect_reference_table.*`) **auto-grows** the per-thread local-ref table to
a hard ceiling `kMaxTableSizeInBytes = 128 MB` → ≈ 8.4M refs (ART 8–13,
16-byte entries) / ≈ 33.5M (ART 14+, 4-byte entries), then `LOG(FATAL)` /
`Runtime::Abort`. At 3 refs/annotation that needs **~3M–11M Link
annotations** → a hundreds-of-MB PDF, which Mattermost upload limits typically
block. So the *clean local-ref-table overflow* is **not** a low-prerequisite
1-PDF kill on real devices. What *is* still reliable: the leak pins millions
of Java objects, so a large-but-smaller PDF exhausts the Java heap
(`OutOfMemoryError`) / triggers Android LMK well before the 128 MB native
ceiling — a genuine but **resource-exhaustion** DoS requiring a large
attachment, not a tiny one. Net: real defect, **Medium**, not the headline.

PoC: `poc/gen_linkbomb_pdf.py` → `poc/linkbomb.pdf` (1500 links),
`poc/linkbomb-min.pdf` (250 links) — well-formed, exercise every leak branch;
the harness proves the exact 3.0 refs/annotation leak rate (it does not, and
now should not, claim a 128-annotation abort).

Fix: hoist class lookups out of the loop; `DeleteLocalRef` `jRectF`, the URI
string, and the Integer object each iteration, or wrap the body in
`PushLocalFrame`/`PopLocalFrame`; bound `annotCount`.

## F5 — HIGH: unchecked `FindClass`/`GetMethodID`; JNI calls continue after pending exception

`native-lib.cpp:329-330`, `:371-377`, and all `env->ThrowNew(env->FindClass(...))`.
CWE-252 / CWE-755.

`linkClass`/`linkCtor` and the per-iteration `RectF`/`Integer` lookups are
never null-checked. A consuming app whose R8/ProGuard config strips or renames
`com.mattermost.pdfium.model.PdfLink` or its `(RectF,String,Integer)` ctor (a
common real-world condition for a JitPack lib) makes `FindClass` return null
with a pending exception; the code then calls `GetMethodID(null,…)`,
`NewObject`, `NewObjectArray` with a pending exception → ART CheckJNI fatal
abort. Availability/robustness DoS, attacker-influenced via F4's link path.

Fix: null-check every `FindClass`/`GetMethodID`; return immediately after
`ThrowNew`; check `ExceptionCheck()` before further JNI.

## F6 — SUPPLY CHAIN (HIGH): bundled PDFium `chromium/7811` is ~12 months stale; update path has no integrity check

`pdfium_version.txt` = `7811` → bblanchon `chromium/7811` = PDFium
`149.0.7811.0`, **cut ~27 Apr 2025**. Latest is `~7834` (May 2026): the binary
that parses untrusted PDFs is **~12.5 months behind**, missing many upstream
memory-safety fixes including publicly disclosed PDFium heap-overflow/RCE CVEs
landed after the cut (e.g. CVE-2026-2648, -4455, -6305, -6306, -6361 — all
"crafted PDF → heap buffer overflow" in the parse/render path this app
exposes). CWE-1395 / CWE-937.

Additionally `scripts/setup-pdfium.sh:38-52` downloads the prebuilt `.so` over
HTTPS with **no checksum or signature verification** (CWE-494): a compromised
release asset or account ships arbitrary native code into the app.

Fix: bump to current PDFium and set a recurring update cadence; pin and verify
a SHA-256 (and ideally signature) of each downloaded archive in the script.

## F7 — LOW: `jstringToStdString` does not check `GetStringUTFChars` for null

`native-lib.cpp:65-71`. CWE-476. Null `jstring` is handled, but a null return
from `GetStringUTFChars` (OOM/pending exception) flows into
`std::string(nullptr)` → UB/crash. Low likelihood, Availability only.

## F8 — CRITICAL: Kotlin non-volatile unsynchronized handle → `close()`/`finalize()` double-free

`PdfBridge.kt:10` (`private var nativeHandle: Long`), `:41-46` (`close()`),
`:48-57` (`finalize()`). CWE-415 / CWE-364 / CWE-667. **Empirically proven (ASan).**

`nativeHandle` is a plain non-`@Volatile` `var`; `close()` runs on an app
thread, `finalize()` runs on the JVM/ART FinalizerDaemon thread. The
check-then-act `if (nativeHandle != 0L) { nativeCloseDocument(...) ; nativeHandle = 0 }`
is unsynchronized. Two threads can both read a non-zero handle and both call
`nativeCloseDocument(sameHandle)` → native `delete wrapper` twice (heap
double-free) and double `FPDF_CloseDocument`. The native side only rejects
`handle == 0`; a freed-but-non-zero pointer passes `if (wrapper)`. Distinct
from F1/F2: the defect is the Kotlin-level unsynchronized non-volatile field
enabling double-entry of `close()` itself. Heap corruption → potential RCE,
reliable crash at minimum. Note `finalize()` literally calls `close()`, and
the API design *encourages* an explicit `close()` too, so the race is normal.

Fix: `AtomicLong` handle with `getAndSet(0L)` (or a lock); only the thread
that wins the swap calls native free; mark the field `@Volatile`.

## F9 — HIGH: uncaught C++ exception across the JNI boundary (attacker-controlled allocation)

`nativeGetLinksForPage` (`:309-390`) and `nativeRenderPageToBitmap` (`:236-307`)
have **no `try/catch`** (unlike `nativeOpenDocument`). CWE-248 / CWE-789.

`native-lib.cpp:359` `std::vector<char> buffer(len);` — `len` is `unsigned long`
from `FPDFAction_GetURIPath` for an **attacker-controlled link-URI** in the
PDF. An oversized/huge URI → `std::length_error`/`std::bad_alloc` thrown
straight out of the JNI function → `std::terminate`/`abort`. Same for
`std::bad_alloc` from `linkObjects` growth or from PDFium internals on a
hostile document. Single malicious PDF → deterministic process kill on the
routine `getLinksForPage()` path; no prerequisites beyond opening the file.

Fix: wrap both functions in the `try/catch(std::exception&)/catch(...)`
pattern used by `nativeOpenDocument`; bound `len` before allocating.

## F10 — MEDIUM: `FPDF_NO_CATCH` disables PDFium fault recovery while rendering attacker PDFs

`native-lib.cpp:299` — `FPDF_RenderPageBitmap(..., FPDF_ANNOT | FPDF_NO_CATCH)`.
CWE-755 / CWE-248. `FPDF_NO_CATCH` disables PDFium's internal setjmp/longjmp
recovery, so a malformed/malicious page that PDFium would normally fail
gracefully on instead aborts the process or continues on corrupt state — a
deliberate mitigation/fault-recovery removal on a fully attacker-controlled
input path. Fix: pass just `FPDF_ANNOT`.

## F11 — MEDIUM (defense-in-depth): bundled PDFium built without Clang CFI / BTI

Binary forensics on all 4 `libpdfium.so` (evidence: `rabin2 -I`, `nm -D`,
`objdump -p`): NX, PIE, **full RELRO + BIND_NOW**, stack canaries, FORTIFY are
all present, and the build is correctly minimized — **no V8/JavaScript and no
XFA forms engine** (eliminates PDFium's two highest-severity scripting
surfaces; consistent with the ~6 MB size). **However there is no Clang CFI and
no AArch64 BTI** (`__cfi_check`/`__cfi_slowpath` absent) — upstream Chromium
Android PDFium normally ships CFI; its absence removes the strongest mitigation
against PDFium's characteristic type-confusion/vtable bugs. The classic
memory-corruption codec surface is fully linked and attacker-reachable:
OpenJPEG (JPEG2000), JBIG2, CCITT fax, libjpeg-turbo, Flate/LZW, FreeType.
Embedded ICU namespace `icu_7811` + `icudt78l` independently corroborate the
claimed `chromium/7811`. CWE-1104 / CWE-693.

Minor/rejected (documented for completeness): forged-`jlong` injection — no
API injection path (stale-pointer case folded into F8); `int y*stride`
overflow in `swapRedBlueChannels` — real UB but needs a >½-billion-px bitmap
Android won't allocate, not PDF-attacker-reachable; `size_t→jsize` narrowing
at `NewObjectArray` — `bad_alloc` (F9) fires first; missing `pageIndex` bound
check in `nativeGetPageSize`/`nativeGetLinksForPage` — PDFium handles it
(defense-in-depth gap only); `FPDF_GetPageCount` per-render — O(1) on the
standard load path, not a reparse-amplification DoS; dead `scaleX/scaleY`
code — correctness, not security.

---

### Severity summary

Ranking corrected after pinning ART semantics (Agent A) and an adversarial
re-review (Agent B). **F1 is the assured headline finding**: deterministic,
single-threaded, no race, no malicious input, ASan-proven. F4 was downgraded
(its ~512/~128 basis was Dalvik-only; see F4).

| ID | Severity | CIA | Prereqs | Status |
|----|----------|-----|---------|--------|
| **F1** | **Critical** | A (det.) / C·I (render path) | 2 docs with overlapping lifetime; **no race, no malicious input** | **ASSURED — ASan UAF, deterministic** (`run.sh f1`,`f1render`) |
| F2 | High | C/I/A | concurrency/GC race | Confirmed (static, 3×); shares F1 root, race variant |
| F8 | High | C/I/A | `close()`/`finalize()` race | **Proven — ASan double-free/UAF** (`run.sh df`); trigger is a race |
| F9 | High | A | 1 PDF, but large file / mem-pressure | Confirmed (static, 2×); DoS only (terminate/OOM-kill) |
| F3 | High | I/A | host bitmap config (not PDF) | Confirmed (static, 3×) |
| F5 | High | A | integrator R8 config | Confirmed (static, 3×) |
| F6 | High | C/I/A | stale dependency | Confirmed (research) |
| F4 | Medium | A | large PDF (millions of annots) | Leak proven (3.0/annot); **not a tiny-PDF kill on ART** |
| F10 | Medium | A/I | render any malicious PDF | Confirmed (static) |
| F11 | Medium | C/I/A | exploit dev (no CFI) | Confirmed (binary forensics) |
| F7 | Low | A | OOM edge | Confirmed (static) |

### Empirical proof harness — `poc/harness/`

`bash poc/harness/run.sh` compiles the **unmodified** `src/main/cpp/native-lib.cpp`
against a mock PDFium (library/document modelled as real heap objects) + a mock
JNIEnv (ART-accurate local-ref accounting), under AddressSanitizer. Results
(reproduced this run):

- **F1** (`run.sh f1`) — the exact idiomatic sequence `open A; open B;
  a.close(); b.isValid(); b.getPageCount()`. `b.isValid()` returns **true**
  (unsound guard), then `FPDF_DestroyLibrary` from `native-lib.cpp:133`
  (closing A) frees library state and `native-lib.cpp:175` on still-open B →
  `AddressSanitizer: heap-use-after-free`. **Deterministic, single-threaded,
  no GC, no race.** Exit 99.
- **F1 render escalation** (`run.sh f1render`) — `open A; open B; a.close();
  open C; b.render()` → re-`InitLibrary` then UAF in the render path
  (`native-lib.cpp:250`). Exit 99.
- **F8 / double-free** (`run.sh df`) — two `nativeCloseDocument(sameHandle)` →
  `heap-use-after-free` at `native-lib.cpp:133`. Proves the native layer has
  **no idempotency guard** (the only defence is the racy Kotlin check); the
  trigger itself is a concurrency race, not deterministic.
- **F4** (`run.sh f4`) — `nativeGetLinksForPage` leaks **exactly 3.0 JNI local
  refs per Link annotation** (752 leaked at N=250, 4502 at N=1500). The
  harness proves the *leak rate*; per the F4 correction it does **not** claim a
  ~128-annotation abort (that was the Dalvik-only figure).
- **F1b** (`run.sh f1b`) — after closing A, opening C re-enters
  `FPDF_InitLibrary` with doc B still live (`initCalls=2, destroyCalls=1`).

F1 (incl. render + f1b variants) and F8 are executed ASan PoCs. F4's leak rate
is empirically measured. F2/F3/F5/F6/F9/F10/F11 are deterministic from code +
PDFium/ART/JNI semantics + binary evidence. Not run on a physical Android
device, but the harness drives the **unmodified** `native-lib.cpp` through the
precise native fault paths that occur on ART.

**Honest scope note:** earlier drafts (a) rated F4 a low-prerequisite 1-PDF
kill — wrong on ART (corrected to Medium); (b) called F8 a caveat-free
critical — its trigger is actually a race (corrected to High). F1 is the
finding that withstood three rounds of adversarial review as genuinely
assured.
