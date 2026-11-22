/* scenario_05.c -- high-volume "overlapping memcpy" differential scenario.
 *
 * Every case uses the ORDINARY C spelling of malloc / memcpy / memmove, made
 * safe by including "stdlib/safe/safe_c.h" first.  The only raw host call in
 * this file is to the genuine (overlap-safe) host memmove, captured as a
 * function pointer *before* the hardened macro names are re-established; it
 * is used exclusively to compute the reference result on a separate pristine
 * copy.  Raw host memcpy is never invoked with overlapping regions here.
 *
 * For each tuple the test allocates a tracked buffer via ordinary malloc,
 * fills it with a deterministic non-zero pattern, computes the expected bytes
 * with host memmove on an independent copy, then runs ordinary memcpy (i.e.
 * cplus_memcpy_s_auto) on the live buffer.  The live buffer must be one of:
 *
 *   (a) exactly the memmove reference   -> copy HANDLED correctly, or
 *   (b) byte-for-byte unchanged         -> REFUSED with no data change, or
 *   (c) exactly the C11 Annex K rejection state: bytes [0, dst_offset)
 *       unchanged and bytes [dst_offset, buf_size) zeroed.  Vendored
 *       safeclib's memcpy_s clears the first dmax bytes of the destination
 *       on ESOVRLP; dmax is the tracked remaining capacity, which for an
 *       interior pointer is buf_size - dst_offset.  This is a deterministic,
 *       documented refusal (the copy never happened, and no byte outside the
 *       destination object changed), so it is counted as a SAFE REFUSAL and
 *       reported as a warning -- never as silent corruption.
 *
 * Anything else (a garbled partial copy, stray writes outside the zeroed
 * destination tail, etc.) is a FINDING with the exact offending tuple.
 * Ordinary memmove is exercised on the same tuple set and must match the host
 * memmove reference exactly.
 *
 * Determinism: the curated part is the full cross product of offset pairs
 * (0..63) x (0..63) x lengths {0,1,2,3,7,15,31,63} = 32768 tuples; the extra
 * randomized part uses splitmix64 with a fixed seed.  Every randomized tuple
 * carries a unique buf_size, so no tuple can collide with another.  No time,
 * address or external state is consulted.  The SafeCRT violation trap is
 * disabled so constraint refusals are survivable.
 */
#define _POSIX_C_SOURCE 200809L

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Capture the real host memmove before restoring the hardened spellings. */
#undef memcpy
#undef memmove
static void *(*const host_memmove_fn)(void *, const void *, size_t) = memmove;

/* Re-establish exactly the mappings from stdlib/safe/safe_c.h so that all
 * ordinary memcpy/memmove calls below go through the hardened wrappers. */
#define memcpy(d, s, n)  ((void)cplus_memcpy_s_auto((d), (s), (n), CPLUS_SAFE_OBJ_SIZE(d)))
#define memmove(d, s, n) ((void)cplus_memmove_s_auto((d), (s), (n), CPLUS_SAFE_OBJ_SIZE(d)))

#define N_CURATED 32768u                  /* 64 * 64 * 8 */
#define N_RANDOM  2048u
#define N_TOTAL   (N_CURATED + N_RANDOM)  /* 34816 distinct memcpy tuples */
#define CURATED_BUF 128u                  /* >= 63 + 63 for every curated pair */

enum direction {
    DIR_SAME     = 0,
    DIR_FORWARD  = 1, /* dst address above src address */
    DIR_BACKWARD = 2, /* dst address below src address */
    DIR_NONE     = 3  /* zero length: no direction */
};

typedef struct {
    size_t   buf_size;
    size_t   src_offset;
    size_t   dst_offset;
    size_t   length;
    int      direction;
    int      overlapping;
    unsigned seed;
} case_t;

static case_t        g_cases[N_TOTAL];
static unsigned long g_findings;
static unsigned long g_overlap_cases;
static unsigned long g_memcpy_handled;
static unsigned long g_memcpy_refused_unchanged;
static unsigned long g_memcpy_refused_zeroed;
static unsigned long g_memmove_ok;
static unsigned long g_warn_printed;
static uint64_t      g_rng = UINT64_C(0xC0FFEE123456789A); /* fixed seed */

/* splitmix64: deterministic, no dependency on libc rand() or time(). */
static uint64_t next64(void) {
    uint64_t z = (g_rng += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static const size_t k_lengths[8] = { 0u, 1u, 2u, 3u, 7u, 15u, 31u, 63u };

static void set_case(case_t *c, size_t bs, size_t s, size_t d, size_t len,
                     unsigned seed) {
    c->buf_size    = bs;
    c->src_offset  = s;
    c->dst_offset  = d;
    c->length      = len;
    c->seed        = seed;
    if (len == 0u) {
        c->direction = DIR_NONE;
    } else if (s == d) {
        c->direction = DIR_SAME;
    } else if (d > s) {
        c->direction = DIR_FORWARD;
    } else {
        c->direction = DIR_BACKWARD;
    }
    c->overlapping = (len != 0u && s < d + len && d < s + len) ? 1 : 0;
}

static void build_cases(void) {
    size_t s, d, k, i;
    i = 0u;
    for (s = 0u; s < 64u; ++s) {
        for (d = 0u; d < 64u; ++d) {
            for (k = 0u; k < 8u; ++k) {
                set_case(&g_cases[i], CURATED_BUF, s, d, k_lengths[k],
                         (unsigned)(UINT32_C(0x9E3779B9) * (uint32_t)(i + 1u)));
                ++i;
            }
        }
    }
    /* Randomized tail.  buf_size = 129 + k is unique across the whole run,
     * so each tuple is distinct even when s/d/length repeat. */
    for (k = 0u; k < N_RANDOM; ++k) {
        size_t  bs   = 129u + k;
        size_t  len  = k_lengths[(size_t)(next64() % 8u)];
        size_t  span = bs - len; /* >= 66, so s+len and d+len stay in range */
        size_t  so   = (size_t)(next64() % (uint64_t)(span + 1u));
        size_t  doff = (size_t)(next64() % (uint64_t)(span + 1u));
        set_case(&g_cases[i], bs, so, doff, len,
                 (unsigned)(next64() & UINT32_C(0xFFFFFFFF)));
        ++i;
    }
    if (i != N_TOTAL) {
        /* Defensive: generation arithmetic must be exact. */
        fprintf(stderr, "FINDING scenario_05 case generation count %lu != %lu\n",
                (unsigned long)i, (unsigned long)N_TOTAL);
        ++g_findings;
    }
}

/* Pattern bytes are in [1,254]: never 0, so a zeroed destination tail can
 * never be confused with an unchanged buffer or with a completed copy. */
static void fill_pattern(unsigned char *p, size_t n, unsigned seed) {
    size_t i;
    for (i = 0u; i < n; ++i) {
        p[i] = (unsigned char)(1u + ((i * 37u + seed) % 254u));
    }
}

static int bytes_equal(const unsigned char *a, const unsigned char *b, size_t n) {
    size_t i;
    for (i = 0u; i < n; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int equals_pattern(const unsigned char *a, size_t n, unsigned seed) {
    size_t i;
    for (i = 0u; i < n; ++i) {
        unsigned char want = (unsigned char)(1u + ((i * 37u + seed) % 254u));
        if (a[i] != want) return 0;
    }
    return 1;
}

/* C11 Annex K / safeclib rejection state: [0,dst) intact, [dst,n) zeroed. */
static int equals_zeroed_tail(const unsigned char *a, size_t n, size_t dst,
                              unsigned seed) {
    size_t i;
    for (i = 0u; i < n; ++i) {
        unsigned char want = (i < dst)
            ? (unsigned char)(1u + ((i * 37u + seed) % 254u))
            : 0u;
        if (a[i] != want) return 0;
    }
    return 1;
}

static void print_tuple(FILE *out, unsigned long idx, const char *api,
                        const case_t *c) {
    fprintf(out, "case=%lu api=%s tuple(buf_size=%lu,src_offset=%lu,"
           "dst_offset=%lu,length=%lu,direction=%d,overlap=%d)\n",
           idx, api, (unsigned long)c->buf_size,
           (unsigned long)c->src_offset, (unsigned long)c->dst_offset,
           (unsigned long)c->length, c->direction, c->overlapping);
}

/* Findings are written to stderr because stdout is muted during the case
 * loop (the vendored ignore handler prints one line per ESOVRLP refusal via
 * printf).  The runner merges both streams into one log. */
static void finding(unsigned long idx, const char *api, const case_t *c,
                    const char *reason, size_t at, unsigned char got,
                    unsigned char want) {
    ++g_findings;
    fputs("FINDING scenario_05 ", stderr);
    print_tuple(stderr, idx, api, c);
    fprintf(stderr, "  reason=%s at=%lu got=0x%02x want=0x%02x\n", reason,
            (unsigned long)at, (unsigned)got, (unsigned)want);
}

static void warn_zeroed(unsigned long idx, const case_t *c) {
    ++g_warn_printed;
    if (g_warn_printed <= 8u) {
        fputs("WARN scenario_05 ESOVRLP-refusal-clears-destination ", stderr);
        print_tuple(stderr, idx, "memcpy", c);
    }
}

static void run_memcpy_case(unsigned long idx) {
    const case_t *c = &g_cases[idx];
    size_t bs  = c->buf_size;
    size_t so  = c->src_offset;
    size_t dof = c->dst_offset;
    size_t len = c->length;
    unsigned char *live, *ref;
    int eq_ref, eq_orig, eq_zero, mismatch_at;

    live = (unsigned char *)malloc(bs);
    ref  = (unsigned char *)malloc(bs);
    if (live == NULL || ref == NULL) {
        free(ref);
        free(live);
        return; /* allocation failure is not a behaviour mismatch */
    }
    fill_pattern(live, bs, c->seed);
    fill_pattern(ref, bs, c->seed);

    /* Reference: real host memmove on the SEPARATE pristine copy.  This is
     * the only unhardened memory call in the test and it is overlap-safe. */
    (void)host_memmove_fn(ref + dof, ref + so, len);

    /* Subject: ordinary memcpy, hardened by safe_c.h. */
    memcpy(live + dof, live + so, len);

    eq_ref  = bytes_equal(live, ref, bs);
    eq_orig = eq_ref ? 0 : equals_pattern(live, bs, c->seed);
    eq_zero = (eq_ref || eq_orig) ? 0
            : equals_zeroed_tail(live, bs, dof, c->seed);

    if (eq_ref) {
        ++g_memcpy_handled;
    } else if (eq_orig) {
        /* A no-change refusal of an overlapping copy is the strictest safe
         * outcome; no FINDING needed. */
        ++g_memcpy_refused_unchanged;
    } else if (eq_zero) {
        ++g_memcpy_refused_zeroed;
        warn_zeroed(idx, c);
    } else {
        /* Silent corruption: report the first differing byte exactly. */
        for (mismatch_at = 0; mismatch_at < (int)bs; ++mismatch_at) {
            if (live[mismatch_at] != ref[mismatch_at]) break;
        }
        finding(idx, "memcpy", c, "neither memmove-exact, unchanged, nor "
                "Annex-K-zeroed refusal",
                (size_t)mismatch_at, live[mismatch_at], ref[mismatch_at]);
    }
    if (c->overlapping) ++g_overlap_cases;

    free(ref);
    free(live);
}

static void run_memmove_case(unsigned long idx) {
    const case_t *c = &g_cases[idx];
    size_t bs  = c->buf_size;
    size_t so  = c->src_offset;
    size_t dof = c->dst_offset;
    size_t len = c->length;
    unsigned char *live, *ref;

    live = (unsigned char *)malloc(bs);
    ref  = (unsigned char *)malloc(bs);
    if (live == NULL || ref == NULL) {
        free(ref);
        free(live);
        return;
    }
    fill_pattern(live, bs, c->seed);
    fill_pattern(ref, bs, c->seed);

    (void)host_memmove_fn(ref + dof, ref + so, len);
    /* Ordinary memmove, hardened by safe_c.h: must match host exactly. */
    memmove(live + dof, live + so, len);

    if (bytes_equal(live, ref, bs)) {
        ++g_memmove_ok;
    } else {
        size_t i;
        for (i = 0u; i < bs; ++i) {
            if (live[i] != ref[i]) break;
        }
        finding(idx, "memmove", c, "hardened memmove differs from host memmove",
                i, live[i], ref[i]);
    }
    free(ref);
    free(live);
}

int main(void) {
    unsigned long i;
    int saved_fd = -1;

    /* Recoverable refusals: never abort on a constraint violation, keep the
     * log quiet, and do not trap into _exit(70) from cplus_runtime_violation. */
    (void)cplus_runtime_set_violation_trap(0);
    (void)cplus_runtime_set_failstop(0);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    errno = 0;

    /* The vendored "ignore" constraint handler prints one line per ESOVRLP
     * refusal with printf().  Mute stdout during the ~70k case loop; findings
     * and the bounded warning samples are written to stderr instead, and the
     * final summary is printed after stdout is restored.  UBSan diagnostics
     * stay visible on stderr throughout. */
    (void)fflush(stdout);
    saved_fd = dup(STDOUT_FILENO);
    {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            (void)dup2(devnull, STDOUT_FILENO);
            (void)close(devnull);
        }
    }

    build_cases();
    for (i = 0u; i < N_TOTAL; ++i) {
        run_memcpy_case(i);
        run_memmove_case(i);
    }

    (void)fflush(stdout);
    if (saved_fd >= 0) {
        (void)dup2(saved_fd, STDOUT_FILENO);
        (void)close(saved_fd);
    }

    printf("scenario_05: memcpy cases=%lu overlap=%lu handled=%lu "
           "refused_unchanged=%lu refused_zeroed=%lu memmove_exact=%lu/%lu "
           "warnings=%lu findings=%lu\n",
           (unsigned long)N_TOTAL, g_overlap_cases, g_memcpy_handled,
           g_memcpy_refused_unchanged, g_memcpy_refused_zeroed,
           g_memmove_ok, (unsigned long)N_TOTAL, g_warn_printed, g_findings);
    if (g_findings != 0u) {
        printf("scenario_05: FAIL (%lu findings)\n", g_findings);
        return 1;
    }
    printf("scenario_05: %lu cases ok\n", 2u * (unsigned long)N_TOTAL);
    return 0;
}
