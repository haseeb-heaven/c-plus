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
 *   responsibility. Never pass an untrusted format string to any of these
 *   calls. There is NO compile-time checking of these wide formats on this
 *   host (see the attribute note below): treating the format as trusted is a
 *   caller obligation, not something the compiler or this layer verifies.
 *
 *   TWO TIERS (see docs/libc/WIDE_FORMAT_CONTRACT.md):
 *   Tier 1 (standard, unchanged): cplus_swprintf / cplus_vswprintf and the
 *     other wide I/O wrappers delegate to the host with standard semantics.
 *     In particular "%n" is NOT detected, rejected or neutralised here:
 *     doing so would silently change standard format semantics.
 *   Tier 2 (opt-in checked sibling): cplus_swprintf_checked additionally
 *     scans the format for a "%n" conversion (including length and POSIX
 *     positional variants, respecting "%%") and refuses it with -1 /
 *     errno = EINVAL *before* touching the va_list or the host formatter,
 *     so the destination is not written on refusal. This is a best-effort
 *     %n deny-list, NOT general format validation: all other format/argument
 *     mismatches remain the caller's responsibility.
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
/* Checked sibling (Tier 2). The wide format attribute below is documentary
 * only on this host -- Clang ignores format(wprintf, ...) -- so the %n scan
 * is runtime, not compile-time. */
int cplus_swprintf_checked(wchar_t *s, size_t n, const wchar_t *fmt, ...) CPLUS_WCHAR_FMT_PRINTF(3, 4);
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

/* Scan a wide format for a conversion whose specifier is 'n'. This is a
 * deny-list for the dangerous %n write-back conversion only; it is NOT a
 * format validator and says nothing about argument count/type agreement.
 * A "%%" is a literal percent and is skipped, so e.g. L"%%n" (which prints
 * the literal text "%n") is not rejected. Length modifiers (hh/h/l/ll/j/z/t/
 * L/q), flags, width, precision, '*' and POSIX positional "N$" specifiers are
 * consumed so variants such as "%hn", "%lln", "%1$n" and "%*2$n" are caught. */
static int cplus_wide_format_has_n(const wchar_t *fmt) {
    const wchar_t *p;
    if (!fmt) return 0;
    for (p = fmt; *p; ) {
        if (*p != L'%') { ++p; continue; }
        ++p;                              /* consume '%' */
        if (*p == L'%') { ++p; continue; } /* "%%" -> literal, not a conversion */
        while (*p == L'-' || *p == L'+' || *p == L'#' || *p == L' ' ||
               *p == L'0' || *p == L'.' || *p == L'*' || *p == L'$' ||
               *p == L'\'' || *p == L'h' || *p == L'l' || *p == L'j' ||
               *p == L'z' || *p == L't' || *p == L'L' || *p == L'q' ||
               (*p >= L'0' && *p <= L'9')) {
            ++p;                          /* flags/width/precision/length/positional */
        }
        if (*p == L'n') return 1;          /* conversion specifier is 'n' */
        if (*p == L'\0') break;            /* trailing '%' -- host handles it */
        ++p;                               /* consume the conversion specifier */
    }
    return 0;
}

int cplus_swprintf_checked(wchar_t *s, size_t n, const wchar_t *fmt, ...) {
    cplus_libc_log("swprintf_checked","enter");
    /* errno is assigned on every refusal *after* logging so an enabled logger
     * (whose write(2) may clobber errno) cannot change the observable result:
     * logging ON and OFF behave identically. */
    if (!fmt || (n > 0 && !s)) { errno = EINVAL; return -1; }
    if (cplus_wide_format_has_n(fmt)) { errno = EINVAL; return -1; }
    va_list ap;
    va_start(ap, fmt);
    int r = vswprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
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
