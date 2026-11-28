/* scenario_10: dynamic allocation size overflow (user's TEST 10).
 *
 * The vulnerable pattern is:
 *
 *     size_t bytes = count * element_size;   // can wrap / be truncated
 *     unsigned char *p = malloc(bytes);      // undersized allocation
 *     for (i = 0; i < count; i++)
 *         memset(p + i * element_size, 0x41, element_size);  // overrun
 *
 * This harness reproduces the exact multiplication (unsigned wrap allowed),
 * calls the ORDINARY malloc()/memset() spelling (mapped by safe_c.h to the
 * capacity-checked wrappers), and verifies that either the allocation fails
 * cleanly or the tracked capacity refuses every write that would overrun.
 * A differential calloc_s(count, element_size) is made for every tuple: the
 * checked allocator must reject the wrapping products with NULL + EOVERFLOW.
 *
 * Build (plain + UBSan), from the repository root:
 *
 *   clang -std=c99 -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     tests/scenarios/scenario_10.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o /tmp/scenario_10
 *   clang ... -fsanitize=undefined ... -o /tmp/scenario_10_ubsan
 */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOTAL_CASES 1200u
#define HASH_BITS   12
#define HASH_SIZE   (1u << HASH_BITS)
#define CANARY_LIMIT (1u << 20)
#define CANARY_BYTE 0xC7

typedef struct {
    size_t count;
    size_t element_size;
    size_t write_len;
    size_t offset;
} case_t;

typedef struct {
    unsigned long wrapping;
    unsigned long reject_expected;
    unsigned long overflow_rejected;
    unsigned long overflow_errno_ok;
    unsigned long malloc_ok;
    unsigned long malloc_clean_fail;
    unsigned long writes_refused;
} stats_t;

static case_t   g_cases[TOTAL_CASES];
static unsigned int g_used[HASH_SIZE]; /* 0 = empty, else case index + 1 */
static unsigned int g_ncases;
static unsigned long g_findings;

/* ------------------------------------------------------------------ */
/* fixed LCG (full period 2^64): non-overlapping quadruples are distinct */
static uint64_t g_lcg = 0x853c49e6748fea9bULL;
static uint64_t next_rand(void) {
    g_lcg = g_lcg * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_lcg;
}

static uint64_t hash_tuple(const case_t *t) {
    const size_t v[4] = { t->count, t->element_size, t->write_len, t->offset };
    uint64_t h = 1469598103934665603ULL;
    int i;
    for (i = 0; i < 4; i++) {
        uint64_t x = (uint64_t)v[i];
        h ^= x;           h *= 1099511628211ULL;
        h ^= (x >> 32);   h *= 1099511628211ULL;
    }
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33;
    return h;
}

/* Returns 1 when the tuple already exists; otherwise stores an empty slot. */
static int tuple_lookup(const case_t *t, unsigned int *slot_out) {
    uint64_t h = hash_tuple(t);
    unsigned int i = (unsigned int)(h & (HASH_SIZE - 1u));
    while (g_used[i]) {
        const case_t *e = &g_cases[g_used[i] - 1u];
        if (e->count == t->count && e->element_size == t->element_size &&
            e->write_len == t->write_len && e->offset == t->offset)
            return 1;
        i = (i + 1u) & (HASH_SIZE - 1u);
    }
    *slot_out = i;
    return 0;
}

static void add_case(size_t count, size_t element_size,
                     size_t write_len, size_t offset) {
    case_t t;
    unsigned int slot;
    if (g_ncases >= TOTAL_CASES) return;
    t.count = count;
    t.element_size = element_size;
    t.write_len = write_len;
    t.offset = offset;
    if (tuple_lookup(&t, &slot)) return;
    g_cases[g_ncases] = t;
    g_used[slot] = g_ncases + 1u;
    g_ncases++;
}

/* ------------------------------------------------------------------ */
static void finding(unsigned int idx, const case_t *t, const char *msg) {
    g_findings++;
    fprintf(stderr,
            "FINDING: case %u (count=%zu element_size=%zu write_len=%zu "
            "offset=%zu bytes=%zu): %s\n",
            idx, t->count, t->element_size, t->write_len, t->offset,
            t->count * t->element_size, msg);
}

static int mul_wraps(size_t count, size_t esize) {
    return esize != 0u && count > SIZE_MAX / esize;
}

/* True when calloc_s() rejects before allocating (overflow or RSIZE_MAX). */
static int calloc_rejects(size_t count, size_t esize) {
    size_t c = count ? count : 1u;
    size_t s = esize ? esize : 1u;
    if (c > SIZE_MAX / s) return 1;
    if (c * s > RSIZE_MAX_MEM) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
static void run_case(unsigned int idx, const case_t *t, stats_t *st) {
    size_t bytes = t->count * t->element_size; /* wrap allowed, size_t */
    unsigned char *p;
    void *q;
    int cerr;

    if (mul_wraps(t->count, t->element_size)) st->wrapping++;

    /* 1) the "bad" computation: ordinary malloc -> checked malloc_s */
    errno = 0;
    p = (unsigned char *)malloc(bytes);
    if (p) st->malloc_ok++; else st->malloc_clean_fail++;

    /* 2) differential: checked calloc_s must reject the wrapping products */
    errno = 0;
    q = calloc_s(t->count, t->element_size);
    cerr = errno;
    if (calloc_rejects(t->count, t->element_size)) {
        st->reject_expected++;
        if (q == NULL) {
            st->overflow_rejected++;
            if (cerr == EOVERFLOW || cerr == EINVAL) {
                st->overflow_errno_ok++;
            } else {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "calloc_s rejected but errno=%d (expected EOVERFLOW/EINVAL)",
                         cerr);
                finding(idx, t, msg);
            }
        } else {
            finding(idx, t, "calloc_s accepted an overflowing product");
            free(q);
        }
    } else if (q != NULL) {
        free(q);
    }
    /* A NULL here for a non-rejecting case is ordinary OOM, not a finding. */

    /* 3) bounded writes through ordinary memset -> checked wrapper.
     *    Canaries sit in the tail of small allocations; writes are confined
     *    to the region before the canary, and a fraction of cases deliberately
     *    ask for a few bytes more than the tracked capacity so the wrapper's
     *    refusal is exercised. No raw (unchecked) OOB is ever issued here. */
    if (p) {
        size_t cap = cplus_safe_ptr_size(p);
        size_t off, wlen, region, avail;
        int have_canary = 0;

        if (cap == 0u) cap = 1u;
        if (cap <= CANARY_LIMIT) {
            if (cap >= 2u) {
                memset(p + cap - 1u, CANARY_BYTE, 1u);
                have_canary = 1;
            }
            region = cap - (have_canary ? 1u : 0u);
            if (region == 0u) region = 1u;
            off = t->offset % region;
            avail = region - off; /* in-range, canary excluded */
            if ((t->write_len & 1u) == 0u) {
                /* strictly more than the tracked remaining capacity
                 * (cap - off): the wrapper must refuse without writing. */
                wlen = (cap - off) + 1u + (t->write_len % 8u);
            } else {
                /* legal write that stays inside the canary-free region */
                wlen = (t->write_len % avail) + 1u;
            }
        } else {
            /* Huge-but-successful allocation: its tracked capacity exceeds
             * RSIZE_MAX_MEM, so request more than that; the wrapper refuses
             * before ever reaching the underlying safeclib call. */
            off = t->offset % 65u;
            wlen = RSIZE_MAX_MEM + 1u;
        }

        memset(p + off, 0x41, wlen); /* ordinary memset, capacity checked */

        if (have_canary && p[cap - 1u] != CANARY_BYTE)
            finding(idx, t, "tail canary corrupted (overrun not contained)");
        if (!have_canary && (t->write_len & 1u) == 0u)
            st->writes_refused++; /* 1-byte block: refusal is the only safe path */

        free(p);
    }
}

/* ------------------------------------------------------------------ */
int main(void) {
    unsigned int i;
    stats_t st;
    char *end;
    size_t u64max;

    /* Diagnostics must not abort the process on a refused write. */
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    u64max = (size_t)UINT64_MAX; /* truncated on 32-bit, SIZE_MAX on 64-bit */

    /* ---- explicit edge tuples -------------------------------------- */
    add_case(0, 0, 0, 0);
    add_case(1, 1, 1, 0);
    add_case(1, 1, 4, 0);                   /* over-capacity request */
    add_case(0, 1, 1, 0);
    add_case(1, 0, 1, 0);
    add_case(SIZE_MAX, 1, 1, 0);
    add_case(SIZE_MAX, 2, 1, 0);            /* wraps */
    add_case(SIZE_MAX / 2u + 1u, 2u, 8u, 1u); /* wraps to 0 */
    add_case(SIZE_MAX / 3u, 3u, 4u, 2u);
    add_case(u64max, 4u, 4u, 3u);           /* UINT64_MAX truncated */
    add_case(0x40000001u, 4u, 4u, 0u);      /* classic 32-bit overflow shape */
    add_case((size_t)strtoull("18446744073709551615", &end, 10), 8u, 8u, 7u);
    add_case((size_t)strtoull("0xffffffffffffffff", &end, 16), 3u, 3u, 1u);
    add_case((size_t)strtoull("-1", &end, 10), 1u, 2u, 0u); /* negative-looking */
    add_case((size_t)strtoull("-2", &end, 10), 2u, 2u, 1u);
    add_case((size_t)strtoull("1000000", &end, 10), 1024u, 8u, 3u);
    add_case((size_t)strtoull("0x100000000", &end, 16), 4u, 4u, 0u);
    add_case(16u, 16u, 256u, 0u);           /* real 256-byte allocation */
    add_case(16u, 16u, 257u, 255u);         /* over-capacity at tail */
    add_case(2u, 3u, 6u, 0u);
    add_case(3u, 3u, 12u, 6u);
    add_case(SIZE_MAX / 2u, 2u, 2u, 0u);    /* product > RSIZE_MAX_MEM */
    add_case(SIZE_MAX / 2u, 1u, 1u, 0u);    /* product == RSIZE_MAX_MEM */
    add_case(1u, SIZE_MAX, 1u, 0u);

    /* ---- LCG-generated distinct tuples ----------------------------- */
    while (g_ncases < TOTAL_CASES) {
        uint64_t a = next_rand(), b = next_rand();
        uint64_t c = next_rand(), d = next_rand();
        add_case((size_t)a, (size_t)b, (size_t)c, (size_t)d);
    }

    memset(&st, 0, sizeof(st));
    for (i = 0; i < g_ncases; i++)
        run_case(i, &g_cases[i], &st);

    fprintf(stderr,
            "scenario_10 stats: cases=%u wrapping=%lu reject_expected=%lu "
            "overflow_rejected=%lu overflow_errno_ok=%lu malloc_ok=%lu "
            "malloc_clean_fail=%lu writes_refused=%lu\n",
            g_ncases, st.wrapping, st.reject_expected, st.overflow_rejected,
            st.overflow_errno_ok, st.malloc_ok, st.malloc_clean_fail,
            st.writes_refused);

    if (g_findings == 0u) {
        printf("scenario_10: %u cases ok\n", g_ncases);
        return 0;
    }
    fprintf(stderr, "scenario_10: %u cases, %lu FINDING(s)\n",
            g_ncases, g_findings);
    return 1;
}
