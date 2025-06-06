package com.mattermost.pdfium

import android.util.Log
import com.mattermost.pdfium.exceptions.DocumentOpenException
import com.mattermost.pdfium.exceptions.InvalidPasswordException
import com.mattermost.pdfium.exceptions.PasswordRequiredException
import com.mattermost.pdfium.model.PdfLink
import java.io.IOException

class PdfBridge private constructor(private var nativeHandle: Long) {

    companion object {
        private const val TAG = "PdfDocument"

        init {
            System.loadLibrary("mattermost_pdfium")
        }

        @JvmStatic
        @Throws(PasswordRequiredException::class, InvalidPasswordException::class, DocumentOpenException::class, IOException::class)
        fun open(path: String, password: String? = null): PdfBridge {
            return try {
                val handle = nativeOpenDocument(path, password)
                PdfBridge(handle)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to open PDF document", e)
                when (e) {
                    is PasswordRequiredException,
                    is InvalidPasswordException,
                    is DocumentOpenException -> throw e
                    else -> throw DocumentOpenException("Failed to open PDF document: ${e.message}", e)
                }
            }
        }

        @JvmStatic
        private external fun nativeOpenDocument(path: String, password: String?): Long
    }

    @Throws(IOException::class, RuntimeException::class)
    fun close() {
        if (nativeHandle != 0L) {
            nativeCloseDocument(nativeHandle)
            nativeHandle = 0
        }
    }

    @Throws(Throwable::class)
    protected fun finalize() {
        try {
            Log.d(TAG, "Closing document cause finalized was called")
            close()
        } catch (e: Throwable) {
            // Log during GC but don't rethrow
            e.printStackTrace()
        }
    }

    @Throws(IOException::class)
    fun isValid(): Boolean {
        return nativeIsDocumentValid(nativeHandle)
    }

    @Throws(IOException::class, IllegalStateException::class)
    fun getPageCount(): Int {
        return nativeGetPageCount(nativeHandle)
    }

    @Throws(IOException::class, IllegalStateException::class)
    fun getPageSize(pageIndex: Int): Pair<Float, Float> {
        val sizeArray = nativeGetPageSize(nativeHandle, pageIndex)
        if (sizeArray == null || sizeArray.size < 2) {
            throw IOException("Failed to retrieve page size")
        }
        return Pair(sizeArray[0], sizeArray[1])
    }
    @Throws(IOException::class, IllegalStateException::class)
    fun renderPageToBitmap(pageIndex: Int, bitmap: android.graphics.Bitmap, scale: Float): Boolean {
        return nativeRenderPageToBitmap(nativeHandle, pageIndex, bitmap, scale)
    }

    @Throws(IOException::class)
    fun getLinksForPage(pageIndex: Int): List<PdfLink> {
        return nativeGetLinksForPage(nativeHandle, pageIndex).toList()
    }

    private external fun nativeCloseDocument(handle: Long)
    private external fun nativeIsDocumentValid(handle: Long): Boolean
    private external fun nativeGetPageCount(handle: Long): Int
    private external fun nativeGetPageSize(handle: Long, pageIndex: Int): FloatArray?
    private external fun nativeRenderPageToBitmap(handle: Long, pageIndex: Int, bitmap: android.graphics.Bitmap, scale: Float): Boolean
    private external fun nativeGetLinksForPage(handle: Long, pageIndex: Int): Array<PdfLink>
}
