/* Platform-backed (no bundled implementations) ISO C17 <wchar.h> numeric
 * conversion, tokenization and time-formatting wrappers for the C+ libc slice.
 *
 * Every cplus_* entry point delegates directly to the identically named host
 * libc function; conversion/base/errno/endptr semantics are exactly those of
 * the host backend. Host backend list:
 *   - wcstol   (host wcstol)
 *   - wcstoll  (host wcstoll)
 *   - wcstoul  (host wcstoul)
 *   - wcstoull (host wcstoull)
 *   - wcstod   (host wcstod)
 *   - wcstof   (host wcstof)
 *   - wcstold  (host wcstold)
 *   - wcstok   (host ISO C17 3-argument wcstok; on Windows, _wcstok)
 *   - wcsftime (host wcsftime)
 *
 * The only behavior added here is a NULL-pointer hardening guard (errno =
 * EINVAL, conventional failure return) for required pointers whose use would
 * otherwise be undefined behavior. Endptr arguments may legitimately be NULL
 * and are never rejected. No host symbol is interposed: only cplus_* names are
 * defined. This file is not a complete implementation of <wchar.h>.
 */

#define _XOPEN_SOURCE 700
#include "../../log/log.h"
#include <errno.h>
#include <stddef.h>
#include <wchar.h>

long cplus_wcstol(const wchar_t *nptr, wchar_t **endptr, int base) {
    cplus_libc_log("wcstol", "enter");
    if (!nptr) {
        if (endptr) *endptr = NULL;
        errno = EINVAL;
        return 0L;
    }
    return wcstol(nptr, endptr, base);
}

long long cplus_wcstoll(const wchar_t *nptr, wchar_t **endptr, int base) {
    cplus_libc_log("wcstoll", "enter");
    if (!nptr) {
        if (endptr) *endptr = NULL;
        errno = EINVAL;
        return 0LL;
    }
    return wcstoll(nptr, endptr, base);
}

unsigned long cplus_wcstoul(const wchar_t *nptr, wchar_t **endptr, int base) {
    cplus_libc_log("wcstoul", "enter");
    if (!nptr) {
        if (endptr) *endptr = NULL;
        errno = EINVAL;
        return 0UL;
    }
    return wcstoul(nptr, endptr, base);
}

unsigned long long cplus_wcstoull(const wchar_t *nptr, wchar_t **endptr, int base) {
    cplus_libc_log("wcstoull", "enter");
    if (!nptr) {
        if (endptr) *endptr = NULL;
        errno = EINVAL;
        return 0ULL;
    }
    return wcstoull(nptr, endptr, base);
}

double cplus_wcstod(const wchar_t *nptr, wchar_t **endptr) {
    cplus_libc_log("wcstod", "enter");
    if (!nptr) {
        if (endptr) *endptr = NULL;
        errno = EINVAL;
        return 0.0;
    }
    return wcstod(nptr, endptr);
}

float cplus_wcstof(const wchar_t *nptr, wchar_t **endptr) {
    cplus_libc_log("wcstof", "enter");
    if (!nptr) {
        if (endptr) *endptr = NULL;
        errno = EINVAL;
        return 0.0f;
    }
    return wcstof(nptr, endptr);
}

long double cplus_wcstold(const wchar_t *nptr, wchar_t **endptr) {
    cplus_libc_log("wcstold", "enter");
    if (!nptr) {
        if (endptr) *endptr = NULL;
        errno = EINVAL;
        return 0.0L;
    }
    return wcstold(nptr, endptr);
}

wchar_t *cplus_wcstok(wchar_t *s, const wchar_t *delim, wchar_t **saveptr) {
    cplus_libc_log("wcstok", "enter");
    /* A NULL 's' is NOT an error: ISO C17 uses it for continuation calls.
     * 'delim' and 'saveptr', however, are required on every call. */
    if (!delim || !saveptr) {
        errno = EINVAL;
        return NULL;
    }
#if defined(_WIN32)
    /* Windows exposes the ISO C17 three-argument form as _wcstok rather than
     * wcstok (MSVC's wcstok is a legacy two-argument function). Delegate to
     * the three-argument variant; if a toolchain lacks it, the build fails
     * loudly instead of silently falling back to a reimplemented tokenizer. */
    return _wcstok(s, delim, saveptr);
#else
    return wcstok(s, delim, saveptr);
#endif
}

size_t cplus_wcsftime(wchar_t *wcs, size_t maxsize, const wchar_t *format,
                      const struct tm *tm) {
    cplus_libc_log("wcsftime", "enter");
    /* 'wcs' is ignored by the host when maxsize == 0, so only guard it when
     * the host would actually write through it. 'format' is always required. */
    if (!format || (!wcs && maxsize != 0)) {
        errno = EINVAL;
        return 0;
    }
    return wcsftime(wcs, maxsize, format, tm);
}
