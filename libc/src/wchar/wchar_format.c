/* Platform-backed <wchar.h> wide formatted-I/O wrappers (C+ libc slice).
 *
 * Each entry point below is a thin, namespaced (cplus_*) wrapper that delegates
 * to the identically-named host function; the v-forms delegate to the host
 * v-form with the caller's va_list. Exact host backends:
 *
 *   cplus_wprintf   -> vwprintf      cplus_vwprintf   -> vwprintf
 *   cplus_fwprintf  -> vfwprintf     cplus_vfwprintf  -> vfwprintf
 *   cplus_swprintf  -> vswprintf     cplus_vswprintf  -> vswprintf
 *   cplus_wscanf    -> vwscanf       cplus_vwscanf    -> vwscanf
 *   cplus_fwscanf   -> vfwscanf      cplus_vfwscanf   -> vfwscanf
 *   cplus_swscanf   -> vswscanf      cplus_vswscanf   -> vswscanf
 *
 * NO FALSE COMPLETENESS CLAIM: this is not a self-contained or "hardened"
 * formatter. All parsing, conversion, locale handling, buffering and error
 * reporting belong to the host C library. These wrappers cover only the twelve
 * ISO C17 wide formatted-I/O functions listed above; they do not interpose,
 * replace or redefine any host symbol, perform no allocations, and add no new
 * format features.
 *
 * FORMAT-STRING CONTRACT (CALLER RESPONSIBILITY):
 *   Format-string validity -- including that every conversion matches the
 *   type and number of the supplied arguments -- remains the caller's
 *   responsibility. In particular, "%n" is NOT detected, rejected or
 *   neutralised here: doing so would silently change standard format
 *   semantics. Never pass an untrusted format string to any of these calls.
 *
 * COMPILER-ENFORCEABLE CONTRACT / ATTRIBUTES:
 *   Following the CPLUS_LIBC_FMT_PRINTF / CPLUS_LIBC_FMT_SCANF macro pattern
 *   in libc/install-include/cplus_libc.h, the variadic wrappers carry
 *   `format(wprintf, m, n)` / `format(wscanf, m, n)` annotations (the wide
 *   attribute kinds). On this host (Apple Clang 16) the wide kinds are not
 *   implemented: Clang emits -Wignored-attributes and ignores them, so the
 *   annotations are documentary here and NO static format checking occurs.
 *   The diagnostic is suppressed locally (pragma) to keep builds warning-free.
 *   The narrow-equivalent kinds (`format(printf, ...)` / `format(scanf, ...)`)
 *   cannot be applied to a `const wchar_t *` format parameter -- Clang rejects
 *   that as "format argument not a string type" -- so those annotations stay
 *   on the narrow wrappers in cplus_libc.h (out of this file's scope) and are
 *   documented here instead of being repeated on these wide signatures.
 *
 * HARDENING EXTENSION (DEFINED-UB INPUTS ONLY):
 *   NULL format strings, NULL destinations (when the buffer size is > 0) and
 *   NULL streams are rejected with errno = EINVAL and a negative/EOF return.
 *   For every valid input the host function is called unchanged, preserving
 *   standard return counts and error behaviour. swprintf with size 0 and a
 *   NULL destination is passed through to the host (no invented semantics).
 */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include "../../log/log.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-attributes"
#define CPLUS_WCHAR_FMT_PRINTF(f, a) __attribute__((format(wprintf, f, a)))
#define CPLUS_WCHAR_FMT_SCANF(f, a)  __attribute__((format(wscanf, f, a)))
#else
/* GCC does not implement the wide format kinds either, and the narrow kinds
 * are invalid on wchar_t* format parameters. Emit no attribute rather than a
 * hard error; the documented caller contract above still applies. */
#define CPLUS_WCHAR_FMT_PRINTF(f, a)
#define CPLUS_WCHAR_FMT_SCANF(f, a)
#endif

/* Annotated prototypes (mirrors the cplus_libc.h macro pattern). */
int cplus_wprintf(const wchar_t *fmt, ...)                    CPLUS_WCHAR_FMT_PRINTF(1, 2);
int cplus_vwprintf(const wchar_t *fmt, va_list ap)            CPLUS_WCHAR_FMT_PRINTF(1, 0);
int cplus_fwprintf(FILE *stream, const wchar_t *fmt, ...)     CPLUS_WCHAR_FMT_PRINTF(2, 3);
int cplus_vfwprintf(FILE *stream, const wchar_t *fmt, va_list ap) CPLUS_WCHAR_FMT_PRINTF(2, 0);
int cplus_swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...) CPLUS_WCHAR_FMT_PRINTF(3, 4);
int cplus_vswprintf(wchar_t *s, size_t n, const wchar_t *fmt, va_list ap) CPLUS_WCHAR_FMT_PRINTF(3, 0);
int cplus_wscanf(const wchar_t *fmt, ...)                     CPLUS_WCHAR_FMT_SCANF(1, 2);
int cplus_vwscanf(const wchar_t *fmt, va_list ap)             CPLUS_WCHAR_FMT_SCANF(1, 0);
int cplus_fwscanf(FILE *stream, const wchar_t *fmt, ...)      CPLUS_WCHAR_FMT_SCANF(2, 3);
int cplus_vfwscanf(FILE *stream, const wchar_t *fmt, va_list ap) CPLUS_WCHAR_FMT_SCANF(2, 0);
int cplus_swscanf(const wchar_t *str, const wchar_t *fmt, ...) CPLUS_WCHAR_FMT_SCANF(2, 3);
int cplus_vswscanf(const wchar_t *str, const wchar_t *fmt, va_list ap) CPLUS_WCHAR_FMT_SCANF(2, 0);

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/* ---- wide stream/string output ---------------------------------------- */

int cplus_wprintf(const wchar_t *fmt, ...) {
    cplus_libc_log("wprintf","enter");
    if (!fmt) { errno = EINVAL; return -1; }
    va_list ap;
    va_start(ap, fmt);
    int r = vwprintf(fmt, ap);
    va_end(ap);
    return r;
}

int cplus_vwprintf(const wchar_t *fmt, va_list ap) {
    cplus_libc_log("vwprintf","enter");
    if (!fmt) { errno = EINVAL; return -1; }
    return vwprintf(fmt, ap);
}

int cplus_fwprintf(FILE *stream, const wchar_t *fmt, ...) {
    cplus_libc_log("fwprintf","enter");
    if (!stream || !fmt) { errno = EINVAL; return -1; }
    va_list ap;
    va_start(ap, fmt);
    int r = vfwprintf(stream, fmt, ap);
    va_end(ap);
    return r;
}

int cplus_vfwprintf(FILE *stream, const wchar_t *fmt, va_list ap) {
    cplus_libc_log("vfwprintf","enter");
    if (!stream || !fmt) { errno = EINVAL; return -1; }
    return vfwprintf(stream, fmt, ap);
}

int cplus_swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...) {
    cplus_libc_log("swprintf","enter");
    if (!fmt || (n > 0 && !s)) { errno = EINVAL; return -1; }
    va_list ap;
    va_start(ap, fmt);
    int r = vswprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int cplus_vswprintf(wchar_t *s, size_t n, const wchar_t *fmt, va_list ap) {
    cplus_libc_log("vswprintf","enter");
    if (!fmt || (n > 0 && !s)) { errno = EINVAL; return -1; }
    return vswprintf(s, n, fmt, ap);
}

/* ---- wide stream/string input ----------------------------------------- */

int cplus_wscanf(const wchar_t *fmt, ...) {
    cplus_libc_log("wscanf","enter");
    if (!fmt) { errno = EINVAL; return EOF; }
    va_list ap;
    va_start(ap, fmt);
    int r = vwscanf(fmt, ap);
    va_end(ap);
    return r;
}

int cplus_vwscanf(const wchar_t *fmt, va_list ap) {
    cplus_libc_log("vwscanf","enter");
    if (!fmt) { errno = EINVAL; return EOF; }
    return vwscanf(fmt, ap);
}

int cplus_fwscanf(FILE *stream, const wchar_t *fmt, ...) {
    cplus_libc_log("fwscanf","enter");
    if (!stream || !fmt) { errno = EINVAL; return EOF; }
    va_list ap;
    va_start(ap, fmt);
    int r = vfwscanf(stream, fmt, ap);
    va_end(ap);
    return r;
}

int cplus_vfwscanf(FILE *stream, const wchar_t *fmt, va_list ap) {
    cplus_libc_log("vfwscanf","enter");
    if (!stream || !fmt) { errno = EINVAL; return EOF; }
    return vfwscanf(stream, fmt, ap);
}

int cplus_swscanf(const wchar_t *str, const wchar_t *fmt, ...) {
    cplus_libc_log("swscanf","enter");
    if (!str || !fmt) { errno = EINVAL; return EOF; }
    va_list ap;
    va_start(ap, fmt);
    int r = vswscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

int cplus_vswscanf(const wchar_t *str, const wchar_t *fmt, va_list ap) {
    cplus_libc_log("vswscanf","enter");
    if (!str || !fmt) { errno = EINVAL; return EOF; }
    return vswscanf(str, fmt, ap);
}
