/* Platform-backed restartable multibyte / wide-stream wrappers (C+ libc slice).
 *
 * Thin, logged, NULL-hardened adapters over the installed host C library. No
 * multibyte conversion is implemented here; every routine delegates to a host
 * backend, named explicitly:
 *   - mbrtowc, wcrtomb, mbsrtowcs, wcsrtombs  -> host <wchar.h> (system libc)
 *   - fputwc, fgetwc, ungetwc                -> host <wchar.h> (system libc)
 *   - putwc / getwc / putwchar / getwchar     -> host fputwc / fgetwc function
 *     forms (a host may #define putwc/getwc/putwchar/getwchar as macros, so we
 *     call the guaranteed-function fputwc/fgetwc entry points directly).
 *
 * Only cplus_* symbols are exported; no host symbol is interposed. Per C17 a
 * NULL destination / endptr state / mbstate_t is valid where noted (length-only
 * queries, state reset, or use of the internal state) and is passed through.
 * Only genuinely required pointers (the endptr object for mbsrtowcs/wcsrtombs
 * and FILE* for stream I/O) are rejected with errno == EINVAL.
 */
#define _XOPEN_SOURCE 700
#include "../../log/log.h"
#include <errno.h>
#include <stdio.h>
#include <wchar.h>

/* --- restartable multibyte conversion ------------------------------------ */

/* pwc == NULL -> length-only query; ps == NULL -> host internal state;
 * s == NULL -> reset (equivalent to mbrtowc(pwc, "", 1, ps)). All valid. */
size_t cplus_mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps) {
    cplus_libc_log("mbrtowc","enter");
    return mbrtowc(pwc, s, n, ps);
}

/* s == NULL -> reset (equivalent to wcrtomb(buf, L'\0', ps)); ps == NULL ->
 * host internal state. Both valid. */
size_t cplus_wcrtomb(char *s, wchar_t wc, mbstate_t *ps) {
    cplus_libc_log("wcrtomb","enter");
    return wcrtomb(s, wc, ps);
}

/* dst == NULL -> length-only query (len ignored, *src left untouched);
 * ps == NULL -> host internal state. The endptr object itself is required. */
size_t cplus_mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps) {
    cplus_libc_log("mbsrtowcs","enter");
    if (!src) { errno = EINVAL; return (size_t)-1; }
    return mbsrtowcs(dst, src, len, ps);
}

/* dst == NULL -> length-only query (len ignored, *src left untouched);
 * ps == NULL -> host internal state. The endptr object itself is required. */
size_t cplus_wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps) {
    cplus_libc_log("wcsrtombs","enter");
    if (!src) { errno = EINVAL; return (size_t)-1; }
    return wcsrtombs(dst, src, len, ps);
}

/* --- wide stream I/O ------------------------------------------------------ */

wint_t cplus_fputwc(wint_t wc, FILE *stream) {
    cplus_libc_log("fputwc","enter");
    if (!stream) { errno = EINVAL; return WEOF; }
    return fputwc(wc, stream);
}

wint_t cplus_fgetwc(FILE *stream) {
    cplus_libc_log("fgetwc","enter");
    if (!stream) { errno = EINVAL; return WEOF; }
    return fgetwc(stream);
}

/* Equivalent to fputwc; host may #define putwc as a macro, so use the
 * guaranteed fputwc function form directly. */
wint_t cplus_putwc(wint_t wc, FILE *stream) {
    cplus_libc_log("putwc","enter");
    if (!stream) { errno = EINVAL; return WEOF; }
    return fputwc(wc, stream);
}

/* Equivalent to fgetwc; host may #define getwc as a macro, so use the
 * guaranteed fgetwc function form directly. */
wint_t cplus_getwc(FILE *stream) {
    cplus_libc_log("getwc","enter");
    if (!stream) { errno = EINVAL; return WEOF; }
    return fgetwc(stream);
}

/* Host putwchar may be a macro; call the fputwc(stdout) function form. */
wint_t cplus_putwchar(wint_t wc) {
    cplus_libc_log("putwchar","enter");
    return fputwc(wc, stdout);
}

/* Host getwchar may be a macro; call the fgetwc(stdin) function form. */
wint_t cplus_getwchar(void) {
    cplus_libc_log("getwchar","enter");
    return fgetwc(stdin);
}

wint_t cplus_ungetwc(wint_t wc, FILE *stream) {
    cplus_libc_log("ungetwc","enter");
    if (!stream) { errno = EINVAL; return WEOF; }
    return ungetwc(wc, stream);
}
