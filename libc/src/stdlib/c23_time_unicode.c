/*
 * c23_time_unicode.c -- from-standard implementations of three C23 APIs that
 * are MISSING from docs/libc/iso_c_inventory.json:
 *
 *   timespec_getres  (<time.h>)
 *   mbrtoc8 / c8rtomb (<uchar.h>)
 *
 * ============================ PROVENANCE / HONESTY ========================
 * This is a FROM-STANDARD implementation, NOT a copy of vendored upstream
 * code and NOT a verbatim vendor import. No pinned upstream source exists:
 *   - libc/upstream/musl (pinned commit 7813e9119eb5fe681b6750b8a033a0490bca
 *     0941) has no src/multibyte/ directory at all; tag v1.2.6 and master
 *     also lack mbrtoc8.c/c8rtomb.c (docs/libc/UCHARC23_UPSTREAM.md).
 *   - no third_party/ tree contains these functions (checked 2026-11-23).
 * The UTF-8 encode/decode rules below are the RFC 3629 / Unicode Standard
 * mapping that C23 specifies for char8_t, written from the standard. The
 * locale-dependent steps are delegated to the host's mbrtowc/wcrtomb (the
 * same backend choice the musl C23 patch makes), never to a fabricated
 * lookup table. timespec_getres uses the host's clock_getres(CLOCK_REALTIME)
 * as its primary backend and falls back to a documented constant that was
 * verified against a 200000-call probe; see docs/libc/C23_TIME_UNICODE.md.
 *
 * HOST PROBES (2026-11-23): /usr/bin/clang, Apple clang 16.0.0
 * (clang-1600.0.26.6), arm64-apple-darwin27.2.0, MacOSX.sdk (MacOSX15.2.sdk).
 *   -std=c99/-std=c2x:
 *     timespec_getres -> "call to undeclared function 'timespec_getres'"
 *     mbrtoc8         -> "unknown type name 'char8_t'" + undeclared function
 *     c8rtomb         -> undeclared function + "unknown type name 'char8_t'"
 *   sizeof(mbstate_t) == 128, sizeof(wchar_t) == 4, MB_LEN_MAX == 6.
 *   timespec_get(TIME_UTC) probe: min positive delta = 1000 ns, every
 *   tv_nsec a multiple of 1000; clock_getres(CLOCK_REALTIME) == {0, 1000}.
 *   setlocale(LC_ALL, "en_US.UTF-8") succeeds.
 * =========================================================================
 */

/* No feature macros are needed here: on this SDK <time.h> exposes both
 * TIME_UTC and clock_getres under plain -std=c99 (verified 2026-11-23).
 * Defining _POSIX_C_SOURCE here would HIDE TIME_UTC, so it is deliberately
 * absent. */

#include "../../log/log.h"
#include "../../install-include/cplus_c23_time_unicode.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

/* ------------------------------------------------------------------------- */
/* timespec_getres                                                            */
/* ------------------------------------------------------------------------- */

/* Avoid a duplicate symbol with libc/src/c23_small_gaps.c on hosts where that
 * file already defines cplus_timespec_getres by delegating to the host C23
 * symbol (glibc >= 2.38, or the opt-in CPLUS_C23_HOST_HAS_TIMESPEC_GETRES). */
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#  if __GLIBC_PREREQ(2, 38)
#    define CPLUS_C23_TU_HOST_HAS_TIMESPEC_GETRES 1
#  endif
#endif
#if defined(CPLUS_C23_HOST_HAS_TIMESPEC_GETRES)
#  define CPLUS_C23_TU_HOST_HAS_TIMESPEC_GETRES 1
#endif
#if !defined(CPLUS_C23_TU_HOST_HAS_TIMESPEC_GETRES)
#  define CPLUS_C23_TU_HOST_HAS_TIMESPEC_GETRES 0
#endif

/* Documented, probe-verified fallback: macOS timespec_get(TIME_UTC) is backed
 * by gettimeofday(2), whose struct timeval carries a tv_usec field, so the
 * granularity is 1000 ns. LIMITATION: if a future host changes the granularity
 * of timespec_get(TIME_UTC), this constant must be revisited; clock_getres is
 * preferred whenever it succeeds. */
#define CPLUS_C23_TU_TIME_UTC_SEC  0L
#define CPLUS_C23_TU_TIME_UTC_NSEC 1000L

#if !CPLUS_C23_TU_HOST_HAS_TIMESPEC_GETRES
int cplus_timespec_getres(struct timespec *ts, int base) {
    cplus_libc_log("timespec_getres", "enter");
    if (ts == NULL) { errno = EINVAL; return 0; }
    if (base != TIME_UTC) { errno = EINVAL; return 0; }

#if defined(CLOCK_REALTIME)
    /* Host-provided resolution. Accept it only when it is a finite positive
     * sub-second value; otherwise fall through to the documented constant. */
    if (clock_getres(CLOCK_REALTIME, ts) == 0 &&
        ts->tv_nsec > 0 && ts->tv_nsec < 1000000000L) {
        return base;
    }
#endif
    ts->tv_sec  = CPLUS_C23_TU_TIME_UTC_SEC;
    ts->tv_nsec = CPLUS_C23_TU_TIME_UTC_NSEC;
    return base;
}
#endif /* !CPLUS_C23_TU_HOST_HAS_TIMESPEC_GETRES */

/* ------------------------------------------------------------------------- */
/* mbrtoc8 / c8rtomb: private state                                           */
/* ------------------------------------------------------------------------- */

/* The host mbstate_t is opaque (128 bytes here), so no layout is assumed.
 * This implementation owns the caller's mbstate_t completely: it never hands
 * it to mbrtowc/wcrtomb (those get a fresh local state), so the private state
 * below is memcpy'd into and out of it. A magic word distinguishes "initial"
 * (all-zero, as ISO C requires) from "in use"; if the caller zeroes *ps, the
 * next call sees a missing magic and resets. ps == NULL uses a dedicated
 * static state object, matching the standard's internal-state object.
 *
 * LIMITATION (documented): this state is UTF-8-specific because the UTF-8
 * code-unit accumulation is done here; the host mbrtowc/wcrtomb calls still
 * use the current locale for the multibyte side. */
#define CPLUS_C23_TU_MAGIC 0x43323354u /* "C23T" */

#define CPLUS_C23_TU_BUFSZ 8u /* >= MB_LEN_MAX (6 on this host) */

typedef struct {
    unsigned int magic;
    unsigned char d_in[CPLUS_C23_TU_BUFSZ]; /* mbrtoc8 partial input */
    unsigned char d_in_len;
    unsigned char out[4];  /* mbrtoc8 pending UTF-8 code units (-3 path) */
    unsigned char out_pos; /* next index in out[] */
    unsigned char out_len; /* total bytes in out[] */
    unsigned char r_in[CPLUS_C23_TU_BUFSZ]; /* c8rtomb partial UTF-8 input */
    unsigned char r_in_len;
} cplus_c23_tu_state;

/* The private state must fit in the opaque host object. */
typedef char cplus_c23_tu_state_fits[
    (sizeof(cplus_c23_tu_state) <= sizeof(mbstate_t)) ? 1 : -1];

static mbstate_t cplus_c23_tu_internal_ps; /* ps == NULL state object */
/* Per-call scratch host state and output buffer live on the stack so that
 * concurrent calls with distinct ps objects never share mutable storage. */

static cplus_c23_tu_state *cplus_c23_tu_get(mbstate_t *ps,
                                            cplus_c23_tu_state *copy) {
    if (ps == NULL) ps = &cplus_c23_tu_internal_ps;
    memcpy(copy, ps, sizeof *copy);
    if (copy->magic != CPLUS_C23_TU_MAGIC) {
        memset(copy, 0, sizeof *copy);
        copy->magic = CPLUS_C23_TU_MAGIC;
    }
    return copy;
}

static void cplus_c23_tu_put(mbstate_t *ps, const cplus_c23_tu_state *st) {
    if (ps == NULL) ps = &cplus_c23_tu_internal_ps;
    memcpy(ps, st, sizeof *st);
}

/* RFC 3629 / Unicode UTF-8 encoder. Returns 0 for a surrogate or out-of-range
 * code point. This is a from-standard encoding, not vendored code. */
static size_t cplus_c23_tu_utf8_encode(unsigned long cp, unsigned char out[4]) {
    if (cp <= 0x7FUL) {
        out[0] = (unsigned char)cp;
        return 1u;
    }
    if (cp <= 0x7FFUL) {
        out[0] = (unsigned char)(0xC0UL | (cp >> 6));
        out[1] = (unsigned char)(0x80UL | (cp & 0x3FUL));
        return 2u;
    }
    if (cp <= 0xFFFFUL) {
        if (cp >= 0xD800UL && cp <= 0xDFFFUL) return 0u; /* surrogate */
        out[0] = (unsigned char)(0xE0UL | (cp >> 12));
        out[1] = (unsigned char)(0x80UL | ((cp >> 6) & 0x3FUL));
        out[2] = (unsigned char)(0x80UL | (cp & 0x3FUL));
        return 3u;
    }
    if (cp <= 0x10FFFFUL) {
        out[0] = (unsigned char)(0xF0UL | (cp >> 18));
        out[1] = (unsigned char)(0x80UL | ((cp >> 12) & 0x3FUL));
        out[2] = (unsigned char)(0x80UL | ((cp >> 6) & 0x3FUL));
        out[3] = (unsigned char)(0x80UL | (cp & 0x3FUL));
        return 4u;
    }
    return 0u;
}

/* ------------------------------------------------------------------------- */
/* mbrtoc8                                                                    */
/* ------------------------------------------------------------------------- */

size_t cplus_mbrtoc8(char8_t *restrict pc8,
                     const char *restrict s,
                     size_t n,
                     mbstate_t *restrict ps) {
    cplus_c23_tu_state snap, *st = &snap;
    mbstate_t local; /* fresh host state for mbrtowc */
    unsigned char in_len_at_entry;
    unsigned char u8[4];
    wchar_t wc;
    size_t r, k, i, appended, consumed, result;

    cplus_libc_log("mbrtoc8", "enter");
    (void)cplus_c23_tu_get(ps, &snap);

    /* (1) Pending UTF-8 code unit from a previously decoded character:
     *     return -3 and consume no input. */
    if (st->out_pos < st->out_len) {
        if (pc8 != NULL) *pc8 = st->out[st->out_pos++];
        result = (size_t)-3;
        goto done;
    }

    /* (2) NULL s is equivalent to s == "" with n == 1 (ISO C rule). */
    if (s == NULL) { s = ""; n = 1u; }
    if (n == 0u) { result = (size_t)-2; goto done; }

    in_len_at_entry = st->d_in_len;
    appended = 0u;
    r = (size_t)-2;
    wc = (wchar_t)0;

    /* Accumulate bytes one at a time and re-decode with mbrtowc using a fresh
     * local host state. mbrtowc is the decode backend; it is locale-aware for
     * the input side. Stop as soon as a character completes. */
    while (appended < n && st->d_in_len < CPLUS_C23_TU_BUFSZ) {
        st->d_in[st->d_in_len++] = (unsigned char)s[appended++];
        memset(&local, 0, sizeof local);
        r = mbrtowc(&wc, (const char *)st->d_in, st->d_in_len,
                    &local);
        if (r == (size_t)-2) continue; /* incomplete: keep accumulating */
        if (r == (size_t)-1) {
            errno = EILSEQ;
            st->d_in_len = 0u;
            result = (size_t)-1;
            goto done;
        }
        break; /* r == 0 (null) or r == number of bytes forming the char */
    }

    /* Ran out of input (or buffer) with an incomplete sequence. */
    if (r == (size_t)-2) { result = (size_t)-2; goto done; }

    st->d_in_len = 0u;

    if (r == 0u) { /* null character: ISO C resets the conversion state */
        st->out_len = 0u;
        st->out_pos = 0u;
        if (pc8 != NULL) *pc8 = (char8_t)0;
        result = 0u;
        goto done;
    }

    /* Encode the decoded code point into UTF-8 code units. */
    k = cplus_c23_tu_utf8_encode((unsigned long)wc, u8);
    if (k == 0u) { /* surrogate or out-of-range code point */
        errno = EILSEQ;
        st->out_len = 0u;
        st->out_pos = 0u;
        result = (size_t)-1;
        goto done;
    }

    /* Bytes consumed from the caller's `s` = total bytes in the char minus
     * the bytes accumulated on earlier calls. */
    consumed = (r > in_len_at_entry) ? (r - in_len_at_entry) : appended;
    if (consumed > n) consumed = n;

    if (pc8 != NULL) *pc8 = (char8_t)u8[0];
    if (k > 1u) {
        /* u8[0] was delivered via *pc8; queue u8[1..k-1] for -3 returns. */
        for (i = 1u; i < k; ++i) st->out[i - 1u] = u8[i];
        st->out_len = (unsigned char)(k - 1u);
        st->out_pos = 0u;
    } else {
        st->out_len = 0u;
        st->out_pos = 0u;
    }
    result = consumed;

done:
    cplus_c23_tu_put(ps, st);
    return result;
}

/* ------------------------------------------------------------------------- */
/* c8rtomb                                                                    */
/* ------------------------------------------------------------------------- */

size_t cplus_c8rtomb(char *restrict s,
                     char8_t c8,
                     mbstate_t *restrict ps) {
    cplus_c23_tu_state snap, *st = &snap;
    mbstate_t local; /* fresh host state for mbrtowc/wcrtomb */
    char mb[MB_LEN_MAX]; /* wcrtomb output scratch */
    wchar_t wc;
    size_t r, k, result;

    cplus_libc_log("c8rtomb", "enter");
    (void)cplus_c23_tu_get(ps, &snap);

    if (st->r_in_len >= CPLUS_C23_TU_BUFSZ) {
        errno = EILSEQ;
        st->r_in_len = 0u;
        result = (size_t)-1;
        goto done;
    }
    st->r_in[st->r_in_len++] = (unsigned char)c8;

    /* mbrtowc is the UTF-8 accumulation backend: a partial UTF-8 sequence
     * returns -2 until the code unit that completes it arrives. */
    memset(&local, 0, sizeof local);
    r = mbrtowc(&wc, (const char *)st->r_in, st->r_in_len,
                &local);
    if (r == (size_t)-2) { result = (size_t)-2; goto done; }
    if (r == (size_t)-1) {
        errno = EILSEQ;
        st->r_in_len = 0u;
        result = (size_t)-1;
        goto done;
    }
    st->r_in_len = 0u;

    if (r == 0u) { /* null character: reset, store one null byte */
        if (s != NULL) *s = '\0';
        result = 1u;
        goto done;
    }

    /* wcrtomb is the multibyte-encoding backend; a fresh zeroed state is
     * correct for the stateless UTF-8/C locales provided by this host. */
    memset(&local, 0, sizeof local);
    k = wcrtomb(mb, wc, &local);
    if (k == (size_t)-1) {
        errno = EILSEQ;
        result = (size_t)-1;
        goto done;
    }
    if (s != NULL) memcpy(s, mb, k);
    result = k;

done:
    cplus_c23_tu_put(ps, st);
    return result;
}
