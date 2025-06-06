#include <jni.h>
#include <fpdfview.h>
#include <fpdf_annot.h>
#include <fpdf_doc.h>
#include <string>
#include <android/bitmap.h>
#include <android/log.h>
#include <mutex>
#include <memory>
#include <exception>

#define LOG_TAG "PDFiumJNI"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Thread-safe initialization guard
std::mutex pdfium_mutex;
bool pdfium_initialized = false;

// Wrapper for FPDF_DOCUMENT with safe RAII destruction
class DocumentWrapper {
public:
    explicit DocumentWrapper(FPDF_DOCUMENT doc) : document(doc) {}

    ~DocumentWrapper() {
        cleanup();
    }

    void cleanup() {
        if (document) {
            FPDF_CloseDocument(document);
            document = nullptr;
        }
        // Cleanup library if initialized
        std::lock_guard<std::mutex> lock(pdfium_mutex);
        if (pdfium_initialized) {
            FPDF_DestroyLibrary();
            pdfium_initialized = false;
        }
    }

    FPDF_DOCUMENT get() const {
        return document;
    }

private:
    FPDF_DOCUMENT document;
};

void swapRedBlueChannels(uint32_t* pixels, int width, int height, int stride) {
    for (int y = 0; y < height; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>((uint8_t*)pixels + y * stride);
        for (int x = 0; x < width; ++x) {
            uint32_t color = row[x];
            uint8_t a = (color >> 24) & 0xFF;
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = (color) & 0xFF;
            row[x] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }
}


// Helper: Convert jstring to std::string safely
std::string jstringToStdString(JNIEnv* env, jstring str) {
    if (!str) return {};
    const char* chars = env->GetStringUTFChars(str, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(str, chars);
    return result;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_mattermost_pdfium_PdfBridge_nativeOpenDocument(JNIEnv* env, jobject /*thiz*/, jstring jPath, jstring jPassword) {
    try {
        std::string path = jstringToStdString(env, jPath);
        std::string password = jPassword ? jstringToStdString(env, jPassword) : "";

        if (path.empty()) {
            env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "File path cannot be empty");
            return 0;
        }

        std::lock_guard<std::mutex> lock(pdfium_mutex);
        if (!pdfium_initialized) {
            FPDF_InitLibrary();
            pdfium_initialized = true;
        }

        FPDF_DOCUMENT doc = FPDF_LoadDocument(path.c_str(), password.empty() ? nullptr : password.c_str());
        if (!doc) {
            unsigned long err = FPDF_GetLastError();
            std::string message = "PDFium error code: " + std::to_string(err);

            if (err == FPDF_ERR_PASSWORD) {
                if (password.empty()) {
                    env->ThrowNew(env->FindClass("com/mattermost/pdfium/exceptions/PasswordRequiredException"), message.c_str());
                } else {
                    env->ThrowNew(env->FindClass("com/mattermost/pdfium/exceptions/InvalidPasswordException"), message.c_str());
                }
            } else {
                env->ThrowNew(env->FindClass("com/mattermost/pdfium/exceptions/DocumentOpenException"), message.c_str());
            }
            return 0;
        }

        auto* wrapper = new (std::nothrow) DocumentWrapper(doc);
        if (!wrapper) {
            FPDF_CloseDocument(doc);
            env->ThrowNew(env->FindClass("java/lang/OutOfMemoryError"), "Failed to allocate DocumentWrapper");
            return 0;
        }

        return reinterpret_cast<jlong>(wrapper);
    } catch (const std::exception& e) {
        env->ThrowNew(env->FindClass("com/mattermost/pdfium/exceptions/DocumentOpenException"), e.what());
        return 0;
    } catch (...) {
        env->ThrowNew(env->FindClass("com/mattermost/pdfium/exceptions/DocumentOpenException"), "Unknown native exception");
        return 0;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_mattermost_pdfium_PdfBridge_nativeCloseDocument(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    if (handle == 0) return;

    try {
        auto* wrapper = reinterpret_cast<DocumentWrapper*>(handle);
        if (wrapper) {
            wrapper->cleanup();  // Explicit cleanup
            delete wrapper;
        }
    } catch (const std::exception& e) {
        LOGE("Exception during close: %s", e.what());
    } catch (...) {
        LOGE("Unknown exception during close");
    }
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_mattermost_pdfium_PdfBridge_nativeIsDocumentValid(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    if (handle == 0) return JNI_FALSE;

    try {
        auto* wrapper = reinterpret_cast<DocumentWrapper*>(handle);
        return (wrapper && wrapper->get() != nullptr) ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) {
        LOGE("Exception during isDocumentValid: %s", e.what());
        return JNI_FALSE;
    } catch (...) {
        LOGE("Unknown exception during isDocumentValid");
        return JNI_FALSE;
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_mattermost_pdfium_PdfBridge_nativeGetPageCount(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    if (handle == 0) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Invalid document handle");
        return 0;
    }

    try {
        auto* wrapper = reinterpret_cast<DocumentWrapper*>(handle);
        if (!wrapper || !wrapper->get()) {
            env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Document is not valid");
            return 0;
        }

        int count = FPDF_GetPageCount(wrapper->get());
        if (count < 0) {
            env->ThrowNew(env->FindClass("java/io/IOException"), "Failed to get page count");
            return 0;
        }

        return count;
    } catch (const std::exception& e) {
        env->ThrowNew(env->FindClass("java/io/IOException"), e.what());
        return 0;
    } catch (...) {
        env->ThrowNew(env->FindClass("java/io/IOException"), "Unknown native exception");
        return 0;
    }
}

extern "C"
JNIEXPORT jfloatArray JNICALL
Java_com_mattermost_pdfium_PdfBridge_nativeGetPageSize(JNIEnv* env, jobject /*thiz*/, jlong handle, jint pageIndex) {
    if (handle == 0) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Invalid document handle");
        return nullptr;
    }

    try {
        auto* wrapper = reinterpret_cast<DocumentWrapper*>(handle);
        if (!wrapper || !wrapper->get()) {
            env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Document is not valid");
            return nullptr;
        }

        FPDF_PAGE page = FPDF_LoadPage(wrapper->get(), pageIndex);
        if (!page) {
            env->ThrowNew(env->FindClass("java/io/IOException"), "Failed to load page");
            return nullptr;
        }

        double width = 0, height = 0;
        FPDF_GetPageSizeByIndex(wrapper->get(), pageIndex, &width, &height);

        FPDF_ClosePage(page);

        jfloatArray sizeArray = env->NewFloatArray(2);
        if (!sizeArray) {
            env->ThrowNew(env->FindClass("java/lang/OutOfMemoryError"), "Failed to allocate result array");
            return nullptr;
        }

        jfloat size[2] = {static_cast<jfloat>(width), static_cast<jfloat>(height)};
        env->SetFloatArrayRegion(sizeArray, 0, 2, size);
        return sizeArray;

    } catch (const std::exception& e) {
        env->ThrowNew(env->FindClass("java/io/IOException"), e.what());
        return nullptr;
    } catch (...) {
        env->ThrowNew(env->FindClass("java/io/IOException"), "Unknown native exception");
        return nullptr;
    }
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_mattermost_pdfium_PdfBridge_nativeRenderPageToBitmap(JNIEnv* env, jobject /*thiz*/, jlong handle, jint pageIndex, jobject bitmap, jfloat scale) {
    if (handle == 0 || !bitmap) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Invalid document handle or bitmap");
        return JNI_FALSE;
    }

    auto* wrapper = reinterpret_cast<DocumentWrapper*>(handle);
    if (!wrapper || !wrapper->get()) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Document is not valid");
        return JNI_FALSE;
    }

    int pageCount = FPDF_GetPageCount(wrapper->get());
    if (pageIndex < 0 || pageIndex >= pageCount) {
        env->ThrowNew(env->FindClass("java/lang/IndexOutOfBoundsException"), "Invalid page index");
        return JNI_FALSE;
    }

    FPDF_PAGE page = FPDF_LoadPage(wrapper->get(), pageIndex);
    if (!page) {
        env->ThrowNew(env->FindClass("java/io/IOException"), "Failed to load page");
        return JNI_FALSE;
    }

    AndroidBitmapInfo bitmapInfo;
    void* pixels = nullptr;
    if (AndroidBitmap_getInfo(env, bitmap, &bitmapInfo) != ANDROID_BITMAP_RESULT_SUCCESS ||
        AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS || !pixels) {
        FPDF_ClosePage(page);
        env->ThrowNew(env->FindClass("java/io/IOException"), "Failed to lock bitmap pixels");
        return JNI_FALSE;
    }

    int targetWidth = static_cast<int>(bitmapInfo.width);
    int targetHeight = static_cast<int>(bitmapInfo.height);

    if (targetWidth <= 0 || targetHeight <= 0) {
        AndroidBitmap_unlockPixels(env, bitmap);
        FPDF_ClosePage(page);
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Bitmap has invalid dimensions");
        return JNI_FALSE;
    }

    FPDF_BITMAP pdfBitmap = FPDFBitmap_CreateEx(
        targetWidth, targetHeight, FPDFBitmap_BGRx, pixels, bitmapInfo.stride);
    if (!pdfBitmap) {
        AndroidBitmap_unlockPixels(env, bitmap);
        FPDF_ClosePage(page);
        env->ThrowNew(env->FindClass("java/io/IOException"), "Failed to create FPDF bitmap");
        return JNI_FALSE;
    }

    // Clear background
    FPDFBitmap_FillRect(pdfBitmap, 0, 0, targetWidth, targetHeight, 0xFFFFFFFF);

    // Calculate rendering scale to match bitmap size
    double pageWidth = 0, pageHeight = 0;
    FPDF_GetPageSizeByIndex(wrapper->get(), pageIndex, &pageWidth, &pageHeight);
    double scaleX = static_cast<double>(targetWidth) / pageWidth;
    double scaleY = static_cast<double>(targetHeight) / pageHeight;

    FPDF_RenderPageBitmap(pdfBitmap, page, 0, 0, targetWidth, targetHeight, 0, FPDF_ANNOT | FPDF_NO_CATCH);
    swapRedBlueChannels(static_cast<uint32_t*>(pixels), targetWidth, targetHeight, bitmapInfo.stride);

    FPDFBitmap_Destroy(pdfBitmap);
    AndroidBitmap_unlockPixels(env, bitmap);
    FPDF_ClosePage(page);

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_com_mattermost_pdfium_PdfBridge_nativeGetLinksForPage(JNIEnv* env, jobject /*thiz*/, jlong handle, jint pageIndex) {
    if (handle == 0) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Invalid document handle");
        return nullptr;
    }

    auto* wrapper = reinterpret_cast<DocumentWrapper*>(handle);
    if (!wrapper || !wrapper->get()) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Document is not valid");
        return nullptr;
    }

    FPDF_PAGE page = FPDF_LoadPage(wrapper->get(), pageIndex);
    if (!page) {
        env->ThrowNew(env->FindClass("java/io/IOException"), "Failed to load page");
        return nullptr;
    }

    jclass linkClass = env->FindClass("com/mattermost/pdfium/model/PdfLink");
    jmethodID linkCtor = env->GetMethodID(linkClass, "<init>", "(Landroid/graphics/RectF;Ljava/lang/String;Ljava/lang/Integer;)V");

    std::vector<jobject> linkObjects;
    int annotCount = FPDFPage_GetAnnotCount(page);
    for (int i = 0; i < annotCount; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot) continue;

        int subtype = FPDFAnnot_GetSubtype(annot);
        if (subtype != FPDF_ANNOT_LINK) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }

        FS_RECTF rect;
        if (!FPDFAnnot_GetRect(annot, &rect)) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }

        std::string uri;
        jint destPage = -1;

        FPDF_LINK link = FPDFAnnot_GetLink(annot);
        if (link) {
            FPDF_ACTION action = FPDFLink_GetAction(link);
            if (action) {
                unsigned long len = FPDFAction_GetURIPath(wrapper->get(), action, nullptr, 0);
                if (len > 0) {
                    std::vector<char> buffer(len);
                    FPDFAction_GetURIPath(wrapper->get(), action, buffer.data(), len);
                    uri.assign(buffer.data());
                }
            } else {
                FPDF_DEST dest = FPDFLink_GetDest(wrapper->get(), link);  // Corrected: Pass document and link
                if (dest) {
                    destPage = FPDFDest_GetDestPageIndex(wrapper->get(), dest);
                }
            }
        }

        jclass rectFClass = env->FindClass("android/graphics/RectF");
        jmethodID rectFCtor = env->GetMethodID(rectFClass, "<init>", "(FFFF)V");
        jobject jRectF = env->NewObject(rectFClass, rectFCtor, rect.left, rect.top, rect.right, rect.bottom);

        jobject jLink = env->NewObject(linkClass, linkCtor, jRectF,
                                       uri.empty() ? nullptr : env->NewStringUTF(uri.c_str()),
                                       destPage >= 0 ? env->NewObject(env->FindClass("java/lang/Integer"), env->GetMethodID(env->FindClass("java/lang/Integer"), "<init>", "(I)V"), destPage) : nullptr);
        linkObjects.push_back(jLink);
        FPDFPage_CloseAnnot(annot);
    }

    jobjectArray result = env->NewObjectArray(linkObjects.size(), linkClass, nullptr);
    for (size_t i = 0; i < linkObjects.size(); ++i) {
        env->SetObjectArrayElement(result, i, linkObjects[i]);
        env->DeleteLocalRef(linkObjects[i]);
    }

    FPDF_ClosePage(page);
    return result;
}
