/* ISO C11 <uchar.h> UTF-16/UTF-32 conversions.
 *
 * SOURCE: musl libc, pinned commit 7813e9119eb5fe681b6750b8a033a0490bca0941.
 *   src/multibyte/mbrtoc16.c
 *   src/multibyte/c16rtomb.c
 *   src/multibyte/mbrtoc32.c
 *   src/multibyte/c32rtomb.c
 * License: MIT (libc/upstream/musl/COPYRIGHT).
 *
 * The four function bodies below are the upstream code verbatim except for:
 *  - symbol renames mbrtoc16/c16rtomb/mbrtoc32/c32rtomb -> cplus_*;
 *  - internal calls mbrtowc/wcrtomb -> the existing cplus_mbrtowc/cplus_wcrtomb
 *    wrappers (same semantics, already sanity-checked in this repo);
 *  - `#include <uchar.h>` replaced by this project's self-contained header
 *    (the host toolchain has no <uchar.h>);
 *  - one opt-in `cplus_libc_log` entry call per public function.
 * No conversion algorithm was written from scratch.
 *
 * The C23 UTF-8 variants (mbrtoc8/c8rtomb) remain BLOCKED: no host symbols,
 * and no musl source at the pinned commit. Do not fabricate them.
 */
#include "../../log/log.h"
#include "../../install-include/cplus_uchar.h"
#include "../../install-include/cplus_libc.h" /* cplus_mbrtowc/cplus_wcrtomb */
#include <errno.h>
#include <wchar.h>

size_t cplus_mbrtoc16(char16_t *restrict pc16, const char *restrict s,
                      size_t n, mbstate_t *restrict ps) {
    cplus_libc_log("mbrtoc16", "enter");
    static unsigned internal_state;
    if (!ps) ps = (void *)&internal_state;
    unsigned *pending = (unsigned *)ps;

    if (!s) return cplus_mbrtoc16(0, "", 1, ps);

    /* mbrtowc states for partial UTF-8 characters have the high bit set;
     * we use nonzero states without high bit for pending surrogates. */
    if ((int)*pending > 0) {
        if (pc16) *pc16 = *pending;
        *pending = 0;
        return -3;
    }

    wchar_t wc;
    size_t ret = cplus_mbrtowc(&wc, s, n, ps);
    if (ret <= 4) {
        if (wc >= 0x10000) {
            *pending = (wc & 0x3ff) + 0xdc00;
            wc = 0xd7c0 + (wc >> 10);
        }
        if (pc16) *pc16 = wc;
    }
    return ret;
}

size_t cplus_c16rtomb(char *restrict s, char16_t c16, mbstate_t *restrict ps) {
    cplus_libc_log("c16rtomb", "enter");
    static unsigned internal_state;
    if (!ps) ps = (void *)&internal_state;
    unsigned *x = (unsigned *)ps;
    wchar_t wc;

    if (!s) {
        if (*x) goto ilseq;
        return 1;
    }

    if (!*x && c16 - 0xd800u < 0x400) {
        *x = (c16 - 0xd7c0) << 10; /* parenthesised: preserve upstream precedence */
        return 0;
    }

    if (*x) {
        if (c16 - 0xdc00u >= 0x400) goto ilseq;
        else wc = *x + c16 - 0xdc00;
        *x = 0;
    } else {
        wc = c16;
    }
    return cplus_wcrtomb(s, wc, 0);

ilseq:
    *x = 0;
    errno = EILSEQ;
    return -1;
}

size_t cplus_mbrtoc32(char32_t *restrict pc32, const char *restrict s,
                      size_t n, mbstate_t *restrict ps) {
    cplus_libc_log("mbrtoc32", "enter");
    static unsigned internal_state;
    if (!ps) ps = (void *)&internal_state;
    if (!s) return cplus_mbrtoc32(0, "", 1, ps);
    wchar_t wc;
    size_t ret = cplus_mbrtowc(&wc, s, n, ps);
    if (ret <= 4 && pc32) *pc32 = wc;
    return ret;
}

size_t cplus_c32rtomb(char *restrict s, char32_t c32, mbstate_t *restrict ps) {
    cplus_libc_log("c32rtomb", "enter");
    return cplus_wcrtomb(s, c32, ps);
}
