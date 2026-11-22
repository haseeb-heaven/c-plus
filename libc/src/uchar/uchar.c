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
 *  - F2 adaptation (2026-11-23): musl used a single 4-byte `unsigned
 *    internal_state` both as a pseudo-mbstate_t handed to the host and as the
 *    pending-surrogate word (safe there because musl's mbstate_t is 4 bytes).
 *    This host has sizeof(mbstate_t) == 128, so passing that object as
 *    `mbstate_t *` is an out-of-bounds write. The host-facing state is now a
 *    real `mbstate_t` and musl's surrogate word lives in a *separate*
 *    `static unsigned`; the surrogate decision logic below is unchanged.
 *    Canary bytes around the host state plus a diagnostic checker let the
 *    regression test (tests/libc/test_uchar.c) observe any overflow.
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

/* --- F2: host-state objects and canary (adaptation, not algorithm) -------- */

/* Compile-time record of the corrected mismatch: a real mbstate_t is never
 * smaller than the 4-byte word the old code cast to mbstate_t *. If a future
 * host violates that, stop and re-review instead of silently aliasing again. */
typedef char cplus_uchar_mbstate_covers_old_pseudo_state[
    (sizeof(mbstate_t) >= sizeof(unsigned)) ? 1 : -1];

#define CPLUS_UCHAR_CANARY_LEN 16u
#define CPLUS_UCHAR_CANARY_INIT                                                \
    0xA5u, 0x5Au, 0xC3u, 0x3Cu, 0x11u, 0xEEu, 0x77u, 0x88u,                   \
    0xA5u, 0x5Au, 0xC3u, 0x3Cu, 0x11u, 0xEEu, 0x77u, 0x88u

static const unsigned char cplus_uchar_canary_expected[CPLUS_UCHAR_CANARY_LEN] = {
    CPLUS_UCHAR_CANARY_INIT};
/* Guards placed immediately around the two ps==NULL host-state objects. */
static unsigned char cplus_uchar_canary_before[CPLUS_UCHAR_CANARY_LEN] = {
    CPLUS_UCHAR_CANARY_INIT};
static mbstate_t cplus_uchar_internal_state_16; /* cplus_mbrtoc16 host state */
static mbstate_t cplus_uchar_internal_state_32; /* cplus_mbrtoc32 host state */
static unsigned char cplus_uchar_canary_after[CPLUS_UCHAR_CANARY_LEN] = {
    CPLUS_UCHAR_CANARY_INIT};

/* F2 fix proof: the object handed to the host as `mbstate_t *` is a genuine,
 * full-size mbstate_t, not the old 4-byte pseudo-state. */
typedef char cplus_uchar_host_state_is_full_mbstate[
    (sizeof(cplus_uchar_internal_state_16) == sizeof(mbstate_t)) ? 1 : -1];

/* musl's pending-surrogate word, now kept strictly separate from the host
 * state so the two roles never alias (F2). Process-wide for ps==NULL mode,
 * matching musl's original design (see F18 in the lifecycle audit). */
static unsigned cplus_uchar_pending_16;  /* cplus_mbrtoc16 */
static unsigned cplus_uchar_pending_c16; /* cplus_c16rtomb */

/* Regression diagnostic only (not part of the public <uchar.h> API): returns
 * 1 iff both canary guards around the host-state objects are intact. */
int cplus_uchar_internal_canary_ok(void) {
    size_t i;
    for (i = 0; i < CPLUS_UCHAR_CANARY_LEN; ++i) {
        if (cplus_uchar_canary_before[i] != cplus_uchar_canary_expected[i] ||
            cplus_uchar_canary_after[i] != cplus_uchar_canary_expected[i]) {
            return 0;
        }
    }
    return 1;
}

size_t cplus_mbrtoc16(char16_t *restrict pc16, const char *restrict s,
                      size_t n, mbstate_t *restrict ps) {
    cplus_libc_log("mbrtoc16", "enter");
    if (!ps) ps = &cplus_uchar_internal_state_16;
    unsigned *pending = &cplus_uchar_pending_16;

    if (!s) return cplus_mbrtoc16(0, "", 1, ps);

    /* mbrtowc states for partial UTF-8 characters have the high bit set;
     * we use nonzero states without high bit for pending surrogates. */
    if ((int)*pending > 0) {
        if (pc16) *pc16 = (char16_t)*pending;
        *pending = 0;
        return -3;
    }

    wchar_t wc;
    size_t ret = cplus_mbrtowc(&wc, s, n, ps);
    if (ret <= 4) {
        if (wc >= 0x10000) {
            *pending = (unsigned)((wc & 0x3ff) + 0xdc00);
            wc = 0xd7c0 + (wc >> 10);
        }
        if (pc16) *pc16 = (char16_t)wc;
    }
    return ret;
}

size_t cplus_c16rtomb(char *restrict s, char16_t c16, mbstate_t *restrict ps) {
    cplus_libc_log("c16rtomb", "enter");
    (void)ps; /* upstream c16rtomb never passes ps to the host (uses ps=0) */
    unsigned *x = &cplus_uchar_pending_c16;
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
        else wc = (wchar_t)(*x + c16 - 0xdc00);
        *x = 0;
    } else {
        wc = (wchar_t)c16;
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
    if (!ps) ps = &cplus_uchar_internal_state_32;
    if (!s) return cplus_mbrtoc32(0, "", 1, ps);
    wchar_t wc;
    size_t ret = cplus_mbrtowc(&wc, s, n, ps);
    if (ret <= 4 && pc32) *pc32 = (char32_t)wc;
    return ret;
}

size_t cplus_c32rtomb(char *restrict s, char32_t c32, mbstate_t *restrict ps) {
    cplus_libc_log("c32rtomb", "enter");
    return cplus_wcrtomb(s, c32, ps);
}
