// Minimal host stub of <android/log.h>.
#pragma once
#include <cstdio>
#include <cstdarg>
#define ANDROID_LOG_ERROR 6
static inline int __android_log_print(int, const char* tag, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[androidlog:%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    return 0;
}
