/* Platform-backed wide-character wrappers for the ISO C <wchar.h> entry
 * points that were absent from the C+ symbol surface
 * (docs/libc/ISO_C_FULL_INVENTORY.md, "MISSING" C95/C99 rows).
 *
 * Each cplus_* function is a thin namespaced delegate to the identically
 * named host function: the host libc is the single source of truth for
 * collation order, wide-string search, transformation, and stream/state
 * semantics. No upstream algorithm is duplicated and no host symbol is
 * interposed. An "enter" marker is emitted through cplus_libc_log() per
 * the C+ logging convention; logging never alters arguments, return values
 * or errno.
 *
 * NULL / error contract (documented, not rewritten):
 *  - ISO C requires valid wide-string / array / FILE pointers for wcscoll,
 *    wcsxfrm, wcschr, wcsrchr, wcspbrk, wcsspn, wcscspn, wmemchr, fgetws
 *    and fputws. Passing NULL for those arguments is undefined behaviour in
 *    raw C; these wrappers deliberately do NOT invent EINVAL-style guards
 *    (unlike some older libc wrappers) and pass arguments straight through.
 *  - mbsinit is the one exception where ISO C defines NULL behaviour:
 *    mbsinit(NULL) reports an initial state and returns nonzero. The host
 *    implementation already honours this, so it is delegated unchanged.
 */
#define _XOPEN_SOURCE 700
#include "../../log/log.h"
#include <stdio.h>
#include <wchar.h>

int cplus_wcscoll(const wchar_t *ws1, const wchar_t *ws2) {
    cplus_libc_log("wcscoll", "enter");
    return wcscoll(ws1, ws2);
}

size_t cplus_wcsxfrm(wchar_t *ws1, const wchar_t *ws2, size_t n) {
    cplus_libc_log("wcsxfrm", "enter");
    return wcsxfrm(ws1, ws2, n);
}

wchar_t *cplus_wcschr(const wchar_t *ws, wchar_t wc) {
    cplus_libc_log("wcschr", "enter");
    return wcschr(ws, wc);
}

wchar_t *cplus_wcsrchr(const wchar_t *ws, wchar_t wc) {
    cplus_libc_log("wcsrchr", "enter");
    return wcsrchr(ws, wc);
}

wchar_t *cplus_wcspbrk(const wchar_t *ws1, const wchar_t *ws2) {
    cplus_libc_log("wcspbrk", "enter");
    return wcspbrk(ws1, ws2);
}

size_t cplus_wcsspn(const wchar_t *ws1, const wchar_t *ws2) {
    cplus_libc_log("wcsspn", "enter");
    return wcsspn(ws1, ws2);
}

size_t cplus_wcscspn(const wchar_t *ws1, const wchar_t *ws2) {
    cplus_libc_log("wcscspn", "enter");
    return wcscspn(ws1, ws2);
}

wchar_t *cplus_wmemchr(const wchar_t *s, wchar_t c, size_t n) {
    cplus_libc_log("wmemchr", "enter");
    return wmemchr(s, c, n);
}

wchar_t *cplus_fgetws(wchar_t *ws, int n, FILE *stream) {
    cplus_libc_log("fgetws", "enter");
    return fgetws(ws, n, stream);
}

/* ISO C signature: fputws returns int (nonnegative on success, negative on
 * error) -- it does NOT return wchar_t*. */
int cplus_fputws(const wchar_t *ws, FILE *stream) {
    cplus_libc_log("fputws", "enter");
    return fputws(ws, stream);
}

int cplus_mbsinit(const mbstate_t *ps) {
    cplus_libc_log("mbsinit", "enter");
    return mbsinit(ps);
}
