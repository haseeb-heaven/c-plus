#include "../safe_format.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

/*
 * Formatting contract (documented in full in safe_format.h):
 *   - Bounded variants (cplus_snprintf_s / cplus_vsnprintf_s and the
 *     cplus_sprintf_s / cplus_vsprintf_s aliases) never write past `dmax` bytes
 *     and always NUL-terminate when dmax > 0.
 *   - On truncation they return the number of bytes that *would* have been
 *     written, excluding the terminating NUL (C99 snprintf semantics); the
 *     destination then holds a valid, NUL-terminated prefix.
 *   - On error they return a negative value and set errno: EINVAL for invalid
 *     arguments (null format, dest==NULL with dmax!=0), otherwise the errno
 *     reported by the underlying C library call.
 *   - Logging is metadata-only: TRACE on entry, ERROR on error (WARN for the
 *     scanf family, where a negative result may just be EOF).
 */

/* Logs one ERROR line and normalizes errno (keeps a more specific libc value). */
static int fmt_error(const char *api, const char *msg) {
    if (errno == 0) errno = EINVAL;
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, msg);
    return -1;
}

int cplus_vsnprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "vsnprintf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "vsnprintf_s", "null format"); cplus_safe_stat_record("vsnprintf_s", 0); return -1; }
    if (!dest && dmax != 0) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "vsnprintf_s", "invalid dest/dmax"); cplus_safe_stat_record("vsnprintf_s", 0); return -1; }
    int n = vsnprintf(dest, dmax, fmt, ap);
    if (n < 0) { cplus_safe_stat_record("vsnprintf_s", 0); return fmt_error("vsnprintf_s", "format error"); }
    cplus_safe_stat_record("vsnprintf_s", 1);
    return n;
}
int cplus_snprintf_s(char *dest, size_t dmax, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "snprintf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = cplus_vsnprintf_s(dest, dmax, fmt, ap);
    va_end(ap);
    cplus_safe_stat_record("snprintf_s", n >= 0);
    return n;
}
int cplus_printf_s(const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "printf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "printf_s", "null format"); cplus_safe_stat_record("printf_s", 0); return -1; }
    va_list ap; va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    if (n < 0) { cplus_safe_stat_record("printf_s", 0); return fmt_error("printf_s", "io/format error"); }
    cplus_safe_stat_record("printf_s", 1);
    return n;
}
int cplus_fprintf_s(FILE *stream, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fprintf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fprintf_s", "null format"); cplus_safe_stat_record("fprintf_s", 0); return -1; }
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(stream, fmt, ap);
    va_end(ap);
    if (n < 0) { cplus_safe_stat_record("fprintf_s", 0); return fmt_error("fprintf_s", "io/format error"); }
    cplus_safe_stat_record("fprintf_s", 1);
    return n;
}
int cplus_scanf_s(const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "scanf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "scanf_s", "null format"); cplus_safe_stat_record("scanf_s", 0); return -1; }
    va_list ap; va_start(ap, fmt);
    int n = vscanf(fmt, ap);
    va_end(ap);
    if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "scanf_s", "io/format error or EOF");
    cplus_safe_stat_record("scanf_s", n >= 0);
    return n;
}
int cplus_fscanf_s(FILE *stream, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fscanf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fscanf_s", "null format"); cplus_safe_stat_record("fscanf_s", 0); return -1; }
    va_list ap; va_start(ap, fmt);
    int n = vfscanf(stream, fmt, ap);
    va_end(ap);
    if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "fscanf_s", "io/format error or EOF");
    cplus_safe_stat_record("fscanf_s", n >= 0);
    return n;
}
int cplus_vsprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "vsprintf_s", "enter");
    /* Alias of the bounded vsnprintf behavior (see header): NOT Annex K. */
    int n = cplus_vsnprintf_s(dest, dmax, fmt, ap);
    cplus_safe_stat_record("vsprintf_s", n >= 0);
    return n;
}
int cplus_sprintf_s(char *dest, size_t dmax, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "sprintf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = cplus_vsprintf_s(dest, dmax, fmt, ap);
    va_end(ap);
    cplus_safe_stat_record("sprintf_s", n >= 0);
    return n;
}
