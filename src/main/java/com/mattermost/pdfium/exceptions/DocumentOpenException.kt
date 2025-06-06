package com.mattermost.pdfium.exceptions

import java.io.IOException

class DocumentOpenException(message: String, cause: Throwable? = null) : IOException(message, cause)
