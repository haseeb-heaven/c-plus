#include "../safe_format.h"
#include "../safe_log.h"
#include <stdarg.h>
#include <stdio.h>

int cplus_vsnprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap) {
    if (!fmt) return -1;
    if (!dest && dmax != 0) return -1;
    int n = vsnprintf(dest, dmax, fmt, ap);
    if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "vsnprintf_s", "format error");
    return n;
}
int cplus_snprintf_s(char *dest, size_t dmax, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = cplus_vsnprintf_s(dest, dmax, fmt, ap);
    va_end(ap);
    return n;
}
int cplus_printf_s(const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "printf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
int cplus_fprintf_s(FILE *stream, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fprintf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}
int cplus_scanf_s(const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "scanf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = vscanf(fmt, ap);
    va_end(ap);
    return n;
}
int cplus_fscanf_s(FILE *stream, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fscanf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = vfscanf(stream, fmt, ap);
    va_end(ap);
    return n;
}
int cplus_vsprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "vsprintf_s", "enter");
    return cplus_vsnprintf_s(dest, dmax, fmt, ap);
}
int cplus_sprintf_s(char *dest, size_t dmax, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "sprintf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = cplus_vsprintf_s(dest, dmax, fmt, ap);
    va_end(ap);
    return n;
}
