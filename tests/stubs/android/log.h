#pragma once
// Host-build stub for the NDK's <android/log.h>. Only what the mod layer uses.
#include <cstdarg>
#include <cstdio>
enum { ANDROID_LOG_UNKNOWN = 0, ANDROID_LOG_DEFAULT, ANDROID_LOG_VERBOSE, ANDROID_LOG_DEBUG,
       ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR, ANDROID_LOG_FATAL };
inline int __android_log_print(int, const char* tag, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::fprintf(stderr, "[%s] ", tag ? tag : "?");
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
    return 0;
}
inline int __android_log_write(int, const char* tag, const char* text) {
    std::fprintf(stderr, "[%s] %s\n", tag ? tag : "?", text ? text : "");
    return 0;
}
