/* Deterministic fuzz harness for the hardened libc string/memory APIs.
 *
 * Scope (all symbols verified exported by build/libc/libcplus_hardened.a):
 *   cplus_memcpy, cplus_memmove, cplus_memset, cplus_memchr, cplus_memrchr,
 *   cplus_strnlen, cplus_strncpy, cplus_strncat.
 *
 * Only in-bounds, well-defined calls are made.  Every source/destination is a
 * stack object with an 8-byte canary before and after the payload; the canaries
 * and the untouched tail of each destination are re-checked after every call.
 * Deliberately invalid pointers and huge n are NOT generated here -- those
 * belong to the boundary/attack suites.  The same operation stream is replayed
 * with logging off and on; both passes must produce the identical digest,
 * proving that the opt-in logger is observationally inert.
 *
 * Fixed LCG seed => identical digest on every run (verified over 5 runs).
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cplus_libc.h"

/* ---------- deterministic generator ---------- */
#define LCG_SEED UINT32_C(0x5EED1234)
#define LCG_MUL UINT32_C(1664525)
#define LCG_ADD UINT32_C(1013904223)

static uint32_t g_state = LCG_SEED;

static uint32_t rnd(void) {
    g_state = g_state * LCG_MUL + LCG_ADD; /* uint32_t wraps: platform-independent */
    return g_state;
}

/* ---------- sizes and guarded objects ---------- */
#define BYTE_CAP 256u  /* max n passed to the byte APIs, and exact-fit size */
#define STR_CAP 320u   /* >= 24-byte prefix + 256 appended + NUL */
#define CANARY_LEN 8u
#define CANARY_BYTE 0xA5u

#define ITERATIONS 300
#define OPS_PER_ITER 8u
#define OPS_PER_PASS (ITERATIONS * OPS_PER_ITER)
#define PASS_COUNT 2u
#define TOTAL_OPS (OPS_PER_PASS * PASS_COUNT)

typedef struct {
    unsigned char pre[CANARY_LEN];
    unsigned char data[BYTE_CAP];
    unsigned char post[CANARY_LEN];
} byte_obj;

typedef struct {
    unsigned char pre[CANARY_LEN];
    char data[STR_CAP];
    unsigned char post[CANARY_LEN];
} str_obj;

static void check_byte_canaries(const byte_obj *o) {
    size_t i;
    for (i = 0; i < CANARY_LEN; ++i) {
        assert(o->pre[i] == CANARY_BYTE);
        assert(o->post[i] == CANARY_BYTE);
    }
}

static void check_str_canaries(const str_obj *o) {
    size_t i;
    for (i = 0; i < CANARY_LEN; ++i) {
        assert(o->pre[i] == CANARY_BYTE);
        assert(o->post[i] == CANARY_BYTE);
    }
}

static void init_byte_obj(byte_obj *o) {
    size_t i;
    memset(o->pre, CANARY_BYTE, sizeof o->pre);
    memset(o->post, CANARY_BYTE, sizeof o->post);
    for (i = 0; i < BYTE_CAP; ++i) o->data[i] = (unsigned char)rnd();
}

static void init_str_obj(str_obj *o, size_t slen) {
    size_t i;
    assert(slen <= 64u);
    memset(o->pre, CANARY_BYTE, sizeof o->pre);
    memset(o->post, CANARY_BYTE, sizeof o->post);
    memset(o->data, 0x5A, sizeof o->data);
    for (i = 0; i < slen; ++i) o->data[i] = (char)('a' + (int)(rnd() % 26u));
    o->data[slen] = '\0';
}

/* n is always in [0, 256].  Iteration 0 forces zero-size, iteration 1 forces
 * exact-fit writes of all 256 bytes in every size-taking API. */
static size_t pick_n(int iter) {
    if (iter == 0) return 0u;
    if (iter == 1) return BYTE_CAP;
    return (size_t)(rnd() % (BYTE_CAP + 1u));
}

/* ---------- digest (FNV-1a 64, never hashes raw pointer addresses) ---------- */
static uint64_t g_digest;

static void hash_bytes(const void *p, size_t n) {
    const unsigned char *q = (const unsigned char *)p;
    while (n--) {
        g_digest ^= (uint64_t)*q++;
        g_digest *= UINT64_C(0x100000001b3);
    }
}

static void hash_u64(uint64_t v) {
    int i;
    for (i = 0; i < 8; ++i) {
        unsigned char b = (unsigned char)(v >> (i * 8));
        hash_bytes(&b, 1);
    }
}

/* Encode a search result as an index, or -1 as a fixed sentinel. */
static void hash_offset(ptrdiff_t v) {
    if (v < 0) hash_u64(UINT64_MAX);
    else hash_u64((uint64_t)v);
}

/* ---------- one op of each API ---------- */
static void run_iteration(int iter) {
    size_t n = pick_n(iter);

    /* 1. cplus_memcpy: distinct objects, exact byte copy, tail untouched. */
    {
        byte_obj src, dst;
        unsigned char snap[BYTE_CAP];
        size_t i;
        init_byte_obj(&src);
        init_byte_obj(&dst);
        memcpy(snap, src.data, BYTE_CAP);
        {
            unsigned char tail[BYTE_CAP];
            memcpy(tail, dst.data, BYTE_CAP);
            assert(cplus_memcpy(dst.data, src.data, n) == dst.data);
            check_byte_canaries(&src);
            check_byte_canaries(&dst);
            for (i = 0; i < n; ++i) assert(dst.data[i] == snap[i]);
            for (i = n; i < BYTE_CAP; ++i) assert(dst.data[i] == tail[i]);
        }
        hash_bytes("M", 1);
        hash_u64((uint64_t)n);
        hash_bytes(&dst, sizeof dst);
    }

    /* 2. cplus_memmove: mostly distinct objects, plus overlapping-but-valid
     *    cases in both directions (forward and backward copy paths). */
    if (iter % 17 == 2) {
        byte_obj o;
        unsigned char src_snap[BYTE_CAP], before[BYTE_CAP];
        size_t m, soff, doff, lo, hi, i;
        void *r;
        init_byte_obj(&o);
        memcpy(before, o.data, BYTE_CAP);
        m = (iter == 2) ? 128u : (size_t)(rnd() % 97u);
        soff = ((iter & 1) != 0) ? 48u : 32u;
        doff = ((iter & 1) != 0) ? 32u : 48u;
        assert(soff + m <= BYTE_CAP && doff + m <= BYTE_CAP);
        memcpy(src_snap, o.data + soff, m);
        r = cplus_memmove(o.data + doff, o.data + soff, m);
        assert(r == o.data + doff);
        check_byte_canaries(&o);
        assert(memcmp(o.data + doff, src_snap, m) == 0);
        lo = (soff < doff) ? soff : doff;
        hi = ((soff > doff) ? soff : doff) + m;
        for (i = 0; i < lo; ++i) assert(o.data[i] == before[i]);
        for (i = hi; i < BYTE_CAP; ++i) assert(o.data[i] == before[i]);
        hash_bytes("V", 1);
        hash_u64((uint64_t)m);
        hash_u64((uint64_t)soff);
        hash_u64((uint64_t)doff);
        hash_bytes(&o, sizeof o);
    } else {
        byte_obj src, dst;
        unsigned char snap[BYTE_CAP], tail[BYTE_CAP];
        size_t i;
        init_byte_obj(&src);
        init_byte_obj(&dst);
        memcpy(snap, src.data, BYTE_CAP);
        memcpy(tail, dst.data, BYTE_CAP);
        assert(cplus_memmove(dst.data, src.data, n) == dst.data);
        check_byte_canaries(&src);
        check_byte_canaries(&dst);
        for (i = 0; i < n; ++i) assert(dst.data[i] == snap[i]);
        for (i = n; i < BYTE_CAP; ++i) assert(dst.data[i] == tail[i]);
        hash_bytes("V", 1);
        hash_u64((uint64_t)n);
        hash_bytes(&dst, sizeof dst);
    }

    /* 3. cplus_memset: fill exactly n bytes, nothing outside. */
    {
        byte_obj dst;
        unsigned char tail[BYTE_CAP];
        int c = (int)(rnd() & 0xFFu);
        size_t i;
        init_byte_obj(&dst);
        memcpy(tail, dst.data, BYTE_CAP);
        assert(cplus_memset(dst.data, c, n) == dst.data);
        check_byte_canaries(&dst);
        for (i = 0; i < n; ++i) assert(dst.data[i] == (unsigned char)c);
        for (i = n; i < BYTE_CAP; ++i) assert(dst.data[i] == tail[i]);
        hash_bytes("S", 1);
        hash_u64((uint64_t)n);
        hash_u64((uint64_t)c);
        hash_bytes(&dst, sizeof dst);
    }

    /* 4/5. cplus_memchr and cplus_memrchr: first/last occurrence or NULL.
     * Source bytes come from a small alphabet so we can force found and
     * not-found paths without ever reading outside the payload. */
    {
        static const unsigned char present[4] = {'A', 'B', 'C', 'D'};
        static const unsigned char absent[4] = {'X', 'Y', 'Z', 'W'};
        byte_obj src;
        unsigned char snap[BYTE_CAP];
        size_t i;
        int want_found = (n > 0u) && ((rnd() & 1u) != 0u);
        int c = (int)(want_found ? present[rnd() % 4u] : absent[rnd() % 4u]);
        void *f, *l;
        ptrdiff_t fi, li;

        init_byte_obj(&src);
        for (i = 0; i < n; ++i) src.data[i] = (unsigned char)present[rnd() % 4u];
        /* Force the found path even when n is tiny (e.g. n == 1). */
        if (want_found) src.data[rnd() % n] = (unsigned char)c;
        memcpy(snap, src.data, BYTE_CAP);
        for (i = 0; i < n; ++i) assert(snap[i] == (unsigned char)present[snap[i] - 'A']);

        f = cplus_memchr(src.data, c, n);
        l = cplus_memrchr(src.data, c, n);
        check_byte_canaries(&src);

        fi = (f == NULL) ? -1 : (ptrdiff_t)((unsigned char *)f - src.data);
        li = (l == NULL) ? -1 : (ptrdiff_t)((unsigned char *)l - src.data);
        if (want_found) {
            assert(f != NULL && l != NULL);
            assert(fi >= 0 && (size_t)fi < n && li >= fi && (size_t)li < n);
            assert(snap[fi] == (unsigned char)c && snap[li] == (unsigned char)c);
            for (i = 0; i < (size_t)fi; ++i) assert(snap[i] != (unsigned char)c);
            for (i = (size_t)li + 1u; i < n; ++i) assert(snap[i] != (unsigned char)c);
        } else {
            assert(f == NULL && l == NULL);
            for (i = 0; i < n; ++i) assert(snap[i] != (unsigned char)c);
        }
        hash_bytes("C", 1);
        hash_offset(fi);
        hash_bytes("R", 1);
        hash_offset(li);
        hash_bytes(&src, sizeof src);
    }

    /* 6. cplus_strnlen: min(n, strlen) on a controlled NUL-terminated object. */
    {
        str_obj src;
        size_t slen = (size_t)(rnd() % 65u);
        size_t got, want;
        init_str_obj(&src, slen);
        want = (n < slen) ? n : slen;
        got = cplus_strnlen(src.data, n);
        assert(got == want);
        check_str_canaries(&src);
        hash_bytes("L", 1);
        hash_u64((uint64_t)got);
        hash_bytes(&src, sizeof src);
    }

    /* 7. cplus_strncpy: copy up to n, zero-fill the remainder, tail intact. */
    {
        str_obj src, dst;
        char snap[STR_CAP], tail[STR_CAP];
        size_t slen = (size_t)(rnd() % 65u);
        size_t i;
        init_str_obj(&src, slen);
        init_str_obj(&dst, 0u);
        memcpy(snap, src.data, STR_CAP);
        memcpy(tail, dst.data, STR_CAP);
        assert(cplus_strncpy(dst.data, src.data, n) == dst.data);
        check_str_canaries(&src);
        check_str_canaries(&dst);
        for (i = 0; i < n; ++i) {
            char want = (i < slen) ? snap[i] : '\0';
            assert(dst.data[i] == want);
        }
        for (i = n; i < STR_CAP; ++i) assert(dst.data[i] == tail[i]);
        hash_bytes("N", 1);
        hash_u64((uint64_t)n);
        hash_bytes(&dst, sizeof dst);
    }

    /* 8. cplus_strncat: append up to n bytes plus one NUL after a prefix. */
    {
        str_obj src, dst;
        char snap[STR_CAP], tail[STR_CAP];
        size_t slen = (size_t)(rnd() % 65u);
        size_t dlen = (size_t)(rnd() % 25u);
        size_t m, i;
        init_str_obj(&src, slen);
        init_str_obj(&dst, dlen);
        memcpy(snap, src.data, STR_CAP);
        memcpy(tail, dst.data, STR_CAP);
        assert(cplus_strncat(dst.data, src.data, n) == dst.data);
        check_str_canaries(&src);
        check_str_canaries(&dst);
        m = (n < slen) ? n : slen;
        assert(dlen + m < STR_CAP);
        assert(dst.data[dlen + m] == '\0');
        for (i = 0; i < dlen; ++i) assert(dst.data[i] == tail[i]);
        for (i = 0; i < m; ++i) assert(dst.data[dlen + i] == snap[i]);
        for (i = dlen + m + 1u; i < STR_CAP; ++i) assert(dst.data[i] == tail[i]);
        hash_bytes("A", 1);
        hash_u64((uint64_t)n);
        hash_u64((uint64_t)m);
        hash_bytes(&dst, sizeof dst);
    }
}

static uint64_t run_pass(int logging_on) {
    int it;
    g_state = LCG_SEED;
    g_digest = UINT64_C(0xcbf29ce484222325);
    cplus_libc_log_set_enabled(logging_on);
    for (it = 0; it < ITERATIONS; ++it) run_iteration(it);
    return g_digest;
}

/* The opt-in logger writes to raw fd 2, so silence that descriptor while the
 * logging-on pass runs; stdout (digest + result line) stays untouched. */
static int g_saved_stderr = -1;

static void silence_logger(void) {
    int devnull;
    g_saved_stderr = dup(STDERR_FILENO);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        (void)dup2(devnull, STDERR_FILENO);
        (void)close(devnull);
    }
}

static void restore_logger(void) {
    if (g_saved_stderr >= 0) {
        (void)dup2(g_saved_stderr, STDERR_FILENO);
        (void)close(g_saved_stderr);
        g_saved_stderr = -1;
    }
}

int main(void) {
    uint64_t digest_off, digest_on;

    digest_off = run_pass(0);
    silence_logger();
    digest_on = run_pass(1);
    restore_logger();
    cplus_libc_log_set_enabled(0);

    assert(digest_off == digest_on); /* logging must not change any result */
    printf("fuzz_libc: digest=%016llx\n", (unsigned long long)digest_off);
    printf("fuzz_libc: %u cases ok\n", (unsigned)TOTAL_OPS);
    return 0;
}
