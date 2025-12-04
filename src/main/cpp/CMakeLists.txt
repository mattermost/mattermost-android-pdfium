cmake_minimum_required(VERSION 3.4.1)

#Define the project
project(mattermost_pdfium C CXX)

# Set target name (adjust as needed)
set(TARGET_NAME mattermost_pdfium)

# Add your native code file(s)
add_library(${TARGET_NAME} SHARED
    native-lib.cpp # Replace with your actual source files
)

# Include PDFium headers (copied by your setup script)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Link prebuilt PDFium shared library
add_library(pdfium SHARED IMPORTED)

set_target_properties(pdfium PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libpdfium.so
)

# Add 16KB page size alignment for Android 15+ compatibility
# See: https://developer.android.com/guide/practices/page-sizes
target_link_options(${TARGET_NAME} PRIVATE
    -Wl,-z,max-page-size=16384
)

# Link native library with PDFium and required Android libraries
target_link_libraries(${TARGET_NAME}
    pdfium
    android
    log
    jnigraphics
)
