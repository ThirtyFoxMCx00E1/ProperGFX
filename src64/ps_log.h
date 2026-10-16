#pragma once
#include <stdio.h>
#include <stdarg.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif
// Failures worth reporting are also appended to <files/ProperGFX>/propergfx_log.txt (set once by main.cpp), so a user without adb can send it.
inline char* PS_LogFilePath() { static char p[640] = ""; return p; }   // one buffer for the whole .so (was per source file: errors from other files never reached the log)
static inline void PS_FileLog(const char* fmt, ...)
{
    char* path = PS_LogFilePath(); if (!path[0]) return;
    FILE* f = fopen(path, "ab"); if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}
static inline void PS_Log(const char* fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "ProperGFX", "%s", buf);
#else
    fprintf(stderr, "[ProperGFX] %s\n", buf);
#endif
}
