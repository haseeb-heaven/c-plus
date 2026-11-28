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

#if !defined(__APPLE__)
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE
#  endif
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _ISOC11_SOURCE
#    define _ISOC11_SOURCE
#  endif
#endif

#include "../../log/log.h"
#include "../../install-include/cplus_c23_time_unicode.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
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

/* The host mbstate_t is opaque, so no layout is assumed for the caller's
 * object. This implementation owns it completely: it never hands it to
 * mbrtowc/wcrtomb (those get a fresh local state), so the private state below
 * is memcpy'd into and out of it. A magic word distinguishes "initial"
 * (all-zero, as ISO C requires) from "in use"; if the caller zeroes *ps, the
 * next call sees a missing magic and resets.
 *
 * ps == NULL selects a per-thread internal state object.  The fallback state
 * is mutable, so one process-wide object would be a data race when unrelated
 * threads call these functions with ps == NULL (#173).  _Thread_local is the
 * C11 spelling; clang/gcc accept __thread in C99 mode, which is how this
 * library is built. */
#if defined(_MSC_VER)
#  define CPLUS_C23_TU_TLS __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define CPLUS_C23_TU_TLS _Thread_local
#else
#  define CPLUS_C23_TU_TLS __thread
#endif
static CPLUS_C23_TU_TLS mbstate_t cplus_c23_tu_internal_ps;
/* Per-call scratch host state and output buffer live on the stack so that
 * concurrent calls with distinct ps objects never share mutable storage. */

/* #173: on compact-mbstate_t hosts state lives in a mutex-protected pool.  A
 * generation travels with the handle and is verified on every get, so a
 * recycled slot can never make a stale caller silently inherit another
 * caller's bytes; the stale handle is diagnosed instead of aliased. */
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

/* Both get() implementations return 0 for a valid/initial state and -1 for a
 * stale pool handle.  *copy is fully initialised on every return path. */
#if defined(__APPLE__)
/* macOS mbstate_t is 128 bytes: inline memcpy directly into *ps. */
typedef char cplus_c23_tu_state_fits[
    (sizeof(cplus_c23_tu_state) <= sizeof(mbstate_t)) ? 1 : -1];

static int cplus_c23_tu_get(mbstate_t *ps, cplus_c23_tu_state *copy) {
    if (ps == NULL) ps = &cplus_c23_tu_internal_ps;
    memcpy(copy, ps, sizeof *copy);
    if (copy->magic != CPLUS_C23_TU_MAGIC) {
        memset(copy, 0, sizeof *copy);
        copy->magic = CPLUS_C23_TU_MAGIC;
    }
    return 0; /* inline state cannot become stale */
}

static void cplus_c23_tu_put(mbstate_t *ps, const cplus_c23_tu_state *st) {
    if (ps == NULL) ps = &cplus_c23_tu_internal_ps;
    memcpy(ps, st, sizeof *st);
}
#else
/* Platforms with compact mbstate_t (e.g. glibc 8-byte mbstate_t): store a
 * compact handle in *ps backed by a mutex-protected pool.  The handle packs
 * slot (8 bits) and generation (24 bits) into one 32-bit word so it still fits
 * an 8-byte mbstate_t next to the magic word. */
#define CPLUS_C23_TU_POOL_SIZE 64u
#define CPLUS_C23_TU_SLOT_MASK 0xFFu
#define CPLUS_C23_TU_GEN_SHIFT 8u
#define CPLUS_C23_TU_GEN_MASK  0xFFFFFFu

typedef struct {
    unsigned int magic;   /* CPLUS_C23_TU_MAGIC */
    unsigned int slotgen; /* low 8 bits: slot; upper 24 bits: generation */
} cplus_c23_tu_handle;

typedef struct {
    cplus_c23_tu_state st;
    unsigned int generation; /* 0 == never allocated */
} cplus_c23_tu_pool_slot;

typedef char cplus_c23_tu_handle_fits[
    (sizeof(cplus_c23_tu_handle) <= sizeof(mbstate_t)) ? 1 : -1];

static cplus_c23_tu_pool_slot cplus_c23_tu_pool[CPLUS_C23_TU_POOL_SIZE];
static unsigned int cplus_c23_tu_next_slot = 0u;
static unsigned int cplus_c23_tu_next_gen = 1u;
static pthread_mutex_t cplus_c23_tu_mu = PTHREAD_MUTEX_INITIALIZER;

/* Caller holds the pool mutex.  Returns a generation not currently live in
 * any pool slot; the bounded scan keeps a recycled slot from ever matching an
 * older handle, even after the counter wraps. */
static unsigned int cplus_c23_tu_new_generation_locked(void) {
    unsigned int i, g;
    for (;;) {
        g = cplus_c23_tu_next_gen++;
        if (g == 0u || g > CPLUS_C23_TU_GEN_MASK) {
            g = 1u;
            cplus_c23_tu_next_gen = 2u;
        }
        for (i = 0u; i < CPLUS_C23_TU_POOL_SIZE; ++i)
            if (cplus_c23_tu_pool[i].generation == g) break;
        if (i == CPLUS_C23_TU_POOL_SIZE) return g;
    }
}

static int cplus_c23_tu_get(mbstate_t *ps, cplus_c23_tu_state *copy) {
    cplus_c23_tu_handle h;
    unsigned int slot, gen;

    if (ps == NULL) ps = &cplus_c23_tu_internal_ps;
    memcpy(&h, ps, sizeof h);
    if (h.magic != CPLUS_C23_TU_MAGIC) { /* zero-initialised / fresh state */
        memset(copy, 0, sizeof *copy);
        copy->magic = CPLUS_C23_TU_MAGIC;
        return 0;
    }
    slot = h.slotgen & CPLUS_C23_TU_SLOT_MASK;
    gen  = h.slotgen >> CPLUS_C23_TU_GEN_SHIFT;
    if (gen == 0u || slot >= CPLUS_C23_TU_POOL_SIZE) {
        memset(copy, 0, sizeof *copy);
        copy->magic = CPLUS_C23_TU_MAGIC;
        return -1;
    }
    pthread_mutex_lock(&cplus_c23_tu_mu);
    if (cplus_c23_tu_pool[slot].generation == gen &&
        cplus_c23_tu_pool[slot].st.magic == CPLUS_C23_TU_MAGIC) {
        *copy = cplus_c23_tu_pool[slot].st;
        pthread_mutex_unlock(&cplus_c23_tu_mu);
        return 0;
    }
    pthread_mutex_unlock(&cplus_c23_tu_mu);
    memset(copy, 0, sizeof *copy);
    copy->magic = CPLUS_C23_TU_MAGIC;
    return -1; /* stale handle: caller must diagnose, never alias */
}

static void cplus_c23_tu_put(mbstate_t *ps, const cplus_c23_tu_state *st) {
    cplus_c23_tu_handle h;
    unsigned int slot, gen;

    if (ps == NULL) ps = &cplus_c23_tu_internal_ps;
    memcpy(&h, ps, sizeof h);
    pthread_mutex_lock(&cplus_c23_tu_mu);
    slot = h.slotgen & CPLUS_C23_TU_SLOT_MASK;
    gen  = h.slotgen >> CPLUS_C23_TU_GEN_SHIFT;
    if (h.magic != CPLUS_C23_TU_MAGIC || gen == 0u ||
        slot >= CPLUS_C23_TU_POOL_SIZE ||
        cplus_c23_tu_pool[slot].generation != gen) {
        gen = cplus_c23_tu_new_generation_locked();
        slot = (cplus_c23_tu_next_slot++) % CPLUS_C23_TU_POOL_SIZE;
        h.magic = CPLUS_C23_TU_MAGIC;
        h.slotgen = slot | (gen << CPLUS_C23_TU_GEN_SHIFT);
        memcpy(ps, &h, sizeof h); /* ps is caller-owned or TLS, not shared */
    }
    cplus_c23_tu_pool[slot].generation = gen;
    cplus_c23_tu_pool[slot].st = *st;
    pthread_mutex_unlock(&cplus_c23_tu_mu);
}
#endif

/* Strict RFC 3629 UTF-8 prefix decoder used by c8rtomb (issue #175).
 *
 * char8_t input is UTF-8 by definition, independent of LC_CTYPE, so it must
 * not be decoded with the locale-dependent mbrtowc(): in the "C"/"POSIX"
 * locale that call rejects every byte >= 0x80.  Returns 1 when a full code
 * point is available (*cp, *need), 0 for a valid-but-incomplete prefix, and
 * -1 for an invalid prefix (overlong, surrogate, > U+10FFFF or bad
 * continuation byte). */
static int cplus_c23_tu_utf8_decode_prefix(const unsigned char *in, size_t len,
                                           unsigned long *cp,
                                           unsigned int *need) {
    unsigned int n, i;
    unsigned long value;
    unsigned char lo = 0x80u, hi = 0xBFu;

    if (len == 0u) return 0;
    if (in[0] <= 0x7Fu) {
        *cp = in[0];
        *need = 1u;
        return 1;
    }
    if (in[0] >= 0xC2u && in[0] <= 0xDFu) {
        n = 2u;
        value = (unsigned long)(in[0] & 0x1Fu);
    } else if (in[0] >= 0xE0u && in[0] <= 0xEFu) {
        n = 3u;
        value = (unsigned long)(in[0] & 0x0Fu);
        if (in[0] == 0xE0u) lo = 0xA0u; /* reject overlong 3-byte forms */
        if (in[0] == 0xEDu) hi = 0x9Fu; /* reject UTF-16 surrogates */
    } else if (in[0] >= 0xF0u && in[0] <= 0xF4u) {
        n = 4u;
        value = (unsigned long)(in[0] & 0x07u);
        if (in[0] == 0xF0u) lo = 0x90u; /* reject overlong 4-byte forms */
        if (in[0] == 0xF4u) hi = 0x8Fu; /* reject > U+10FFFF */
    } else {
        return -1; /* 0x80..0xBF continuation, 0xC0/0xC1, 0xF5..0xFF */
    }

    /* Validate every continuation byte that has arrived so far.  The first
     * continuation byte carries the overlong/surrogate/range restrictions. */
    for (i = 1u; i < n && i < (unsigned int)len; ++i) {
        unsigned char b = in[i];
        unsigned char blo = (i == 1u) ? lo : 0x80u;
        unsigned char bhi = (i == 1u) ? hi : 0xBFu;
        if (b < blo || b > bhi) return -1;
        value = (value << 6) | (unsigned long)(b & 0x3Fu);
    }
    if ((unsigned int)len < n) return 0; /* valid prefix, not complete yet */
    if (value > 0x10FFFFUL || (value >= 0xD800UL && value <= 0xDFFFUL))
        return -1;
    *cp = value;
    *need = n;
    return 1;
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
    if (cplus_c23_tu_get(ps, &snap) != 0) {
        /* Stale pool handle: fail closed instead of silently inheriting
         * another caller's conversion state (#173). */
        errno = EILSEQ;
        return (size_t)-1;
    }

    /* ISO C23 7.30.1.1: mbrtoc8(pc8, NULL, n, ps) is exactly
     * mbrtoc8(NULL, "", 1, ps).  Do this before the pending-code-unit check so
     * a reset query can neither write through pc8 nor deliver buffered output
     * (#174). */
    if (s == NULL) {
        pc8 = NULL;
        s = "";
        n = 1u;
    }

    /* (1) Pending UTF-8 code unit from a previously decoded character:
     *     return -3 and consume no input.  The buffered position advances even
     *     when pc8 == NULL; otherwise a drain loop would never make progress. */
    if (st->out_pos < st->out_len) {
        unsigned char unit = st->out[st->out_pos++];
        if (pc8 != NULL) *pc8 = (char8_t)unit;
        result = (size_t)-3;
        goto done;
    }

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

    /* #174: a full d_in[] with no completed character is an overlong/invalid
     * sequence, not an endless source of (size_t)-2.  Reject it once. */
    if (r == (size_t)-2 && st->d_in_len >= CPLUS_C23_TU_BUFSZ) {
        errno = EILSEQ;
        st->d_in_len = 0u;
        result = (size_t)-1;
        goto done;
    }

    /* Ran out of caller input with an incomplete sequence. */
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
    mbstate_t local; /* fresh host state for wcrtomb */
    char mb[MB_LEN_MAX]; /* wcrtomb output scratch */
    unsigned long cp;
    unsigned int need;
    size_t k, result;
    int dec;

    cplus_libc_log("c8rtomb", "enter");

    /* ISO C23 7.30.1.2: c8rtomb(NULL, c8, ps) is equivalent to
     * c8rtomb(buf, u8'\0', ps).  Reset the state, ignore c8 and report the one
     * byte the equivalent call would have stored (#175). */
    if (s == NULL) {
        memset(&snap, 0, sizeof snap);
        snap.magic = CPLUS_C23_TU_MAGIC;
        result = 1u;
        goto done;
    }

    if (cplus_c23_tu_get(ps, &snap) != 0) {
        errno = EILSEQ; /* stale pool handle: fail closed (#173) */
        return (size_t)-1;
    }

    if (st->r_in_len >= CPLUS_C23_TU_BUFSZ) {
        errno = EILSEQ;
        st->r_in_len = 0u;
        result = (size_t)-1;
        goto done;
    }
    st->r_in[st->r_in_len++] = (unsigned char)c8;

    /* #175: decode char8_t input as UTF-8 directly, independent of LC_CTYPE.
     * mbrtowc() is locale-dependent and rejects every non-ASCII byte in the
     * "C"/"POSIX" locale. */
    dec = cplus_c23_tu_utf8_decode_prefix(st->r_in, st->r_in_len, &cp, &need);
    if (dec < 0) {
        errno = EILSEQ;
        st->r_in_len = 0u;
        result = (size_t)-1;
        goto done;
    }
    if (dec == 0) {
        /* Valid but incomplete UTF-8 prefix: ISO C23 c8rtomb stores zero
         * bytes and returns 0 -- NEVER (size_t)-2. */
        result = 0u;
        goto done;
    }
    st->r_in_len = 0u;
    (void)need; /* informational: the prefix is complete */

    if ((unsigned long)(wchar_t)cp != cp) { /* wchar_t too narrow for cp */
        errno = EILSEQ;
        result = (size_t)-1;
        goto done;
    }

    /* wcrtomb performs the locale-dependent UTF-8 -> multibyte conversion. */
    memset(&local, 0, sizeof local);
    k = wcrtomb(mb, (wchar_t)cp, &local);
    if (k == (size_t)-1) {
        errno = EILSEQ;
        result = (size_t)-1;
        goto done;
    }
    memcpy(s, mb, k);
    result = k;

done:
    cplus_c23_tu_put(ps, &snap);
    return result;
}
