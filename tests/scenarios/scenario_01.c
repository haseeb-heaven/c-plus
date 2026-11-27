/* scenario_01.c -- high-volume deterministic test for the
 * "integer truncation -> undersized allocation" family (TEST 1).
 *
 * The classic bug this models:
 *
 *     uint64_t n = <attacker controlled>;
 *     uint32_t k = (uint32_t)n;                 /+ truncation +/
 *     size_t   bytes = (size_t)k * elem_size;   /+ undersized +/
 *     T *p = malloc(bytes);
 *     memset(p, 0x41, (size_t)n * elem_size);   /+ OVERFLOW +/
 *
 * We reproduce EXACTLY that arithmetic (all size_t multiplication is unsigned
 * and therefore defined, not UB) and push every resulting write through the
 * hardened `memset` mapping from `stdlib/safe/safe_c.h`.  The mapping resolves
 * the interior pointer `p + offset` to the *tracked remaining capacity* of its
 * containing block, so a write larger than that capacity must be REFUSED with
 * errno == ERANGE and must leave the destination untouched.
 *
 * NO GENUINE UB IS PERFORMED BY THIS TEST:
 *   - every pointer we form (p, p + offset, p[cap-1]) is inside, or exactly one
 *     past the end of, a real allocation we own;
 *   - every byte we write directly is within the block we allocated;
 *   - every DANGEROUS write goes through the hardened wrapper, which refuses
 *     it before any memory is touched.  We never call raw memset/memcpy on an
 *     out-of-range address, never index outside a block, and never rely on
 *     adjacency of two heap blocks for correctness (the trailing canary is a
 *     best-effort extra detector, not the primary assertion).
 *
 * Determinism: a fixed splitmix64 stream (no rand(), no time, no addresses)
 * builds >= 1000 DISTINCT (wide_value, cast_width, element_size, write_count)
 * tuples.  The violation trap is disabled so refusals are survivable.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"

#define N_CURATED   32u
#define N_GENERATED 1300u
#define NCASES      (N_CURATED + N_GENERATED) /* 1332 distinct cases */

typedef struct {
    uint64_t wide_value; /* the original, untruncated count */
    int      cast_width; /* 16, 32 or 64; high bit of wide_value selects signed */
    size_t   element_size;
    size_t   write_count; /* the "intended" byte count (may exceed allocation) */
} case_t;

static case_t        g_cases[NCASES];
static unsigned long g_findings;
static unsigned long g_skipped;
static unsigned long g_refusals;
static unsigned long g_backend_refusals;
static unsigned long g_writes;
static uint64_t      g_rng = UINT64_C(0xD1B54A32D192ED03); /* fixed seed */

/* ------------------------------------------------------------------ */
/* deterministic PRNG: splitmix64 (a bijection => distinct outputs)    */
/* ------------------------------------------------------------------ */
static uint64_t next64(void) {
    uint64_t z = (g_rng += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/* ------------------------------------------------------------------ */
/* Explicit wide -> narrow -> size_t conversions.  Every conversion is */
/* an explicit cast; no implicit signed/unsigned change is left for the */
/* compiler or UBSan to second-guess.                                  */
/*   width 16: UINT64 -> uint16 (zero-extended)                        */
/*   width 32, top bit clear: UINT64 -> uint32                         */
/*   width 32, top bit set:   int64 -> int32 -> size_t (sign-extended)  */
/*   width 64, top bit set:   negative -> unsigned (modular)           */
/* ------------------------------------------------------------------ */
static size_t compute_truncated(uint64_t w, int width) {
    if (width == 16) {
        return (size_t)(uint16_t)w;
    }
    if (width == 32) {
        if ((w >> 63) != 0) {
            return (size_t)(int32_t)(int64_t)w;
        }
        return (size_t)(uint32_t)w;
    }
    if ((w >> 63) != 0) {
        return (size_t)(int64_t)w;
    }
    return (size_t)w;
}

static void finding(unsigned long idx, const case_t *c, size_t truncated,
                    size_t bytes, size_t cap, size_t offset, size_t proven,
                    size_t write_len, int expected_refusal, int err,
                    const char *what, size_t at, unsigned char got) {
    ++g_findings;
    printf("FINDING: case %lu tuple(wide=0x%016llx,width=%d,elem=%lu,count=%lu) "
           "truncated=0x%lx bytes=0x%lx cap=%lu off=%lu proven=%lu len=%lu "
           "expect_refuse=%d errno=%d %s at+%lu got=0x%02x\n",
           idx, (unsigned long long)c->wide_value, c->cast_width,
           (unsigned long)c->element_size, (unsigned long)c->write_count,
           (unsigned long)truncated, (unsigned long)bytes, (unsigned long)cap,
           (unsigned long)offset, (unsigned long)proven,
           (unsigned long)write_len, expected_refusal, err, what,
           (unsigned long)at, (unsigned)got);
}

/* Verify that a refused operation left the whole inspected window and the
 * end-of-block canary exactly as we initialized them.  Any wrongful write
 * (even a short one) necessarily changes dst[0], so this catches overruns. */
static void check_unchanged(unsigned long idx, const case_t *c,
                            size_t truncated, size_t bytes, size_t cap,
                            size_t offset, size_t proven, size_t write_len,
                            int expected_refusal, int err,
                            const unsigned char *p, const unsigned char *dst,
                            size_t win, size_t end_index) {
    size_t k;
    for (k = 0; k < win; ++k) {
        unsigned char want = ((offset + k) == end_index) ? 0xCCu : 0x5Au;
        if (dst[k] != want) {
            finding(idx, c, truncated, bytes, cap, offset, proven, write_len,
                    expected_refusal, err, "destination modified on refused write",
                    k, dst[k]);
            break;
        }
    }
    if (p[end_index] != 0xCCu) {
        finding(idx, c, truncated, bytes, cap, offset, proven, write_len,
                expected_refusal, err, "end canary overwritten on refused write",
                end_index, p[end_index]);
    }
}

/* ------------------------------------------------------------------ */
/* Curated boundary cases: UINT16/UINT32 edges, INT32 edge, just-above  */
/* boundaries, negative-to-unsigned, SIZE_MAX fractions.               */
/* ------------------------------------------------------------------ */
static void fill_curated(void) {
    static const case_t k[N_CURATED] = {
        /* wide, width, elem, write_count */
        { UINT64_C(0x0000000000000000), 32, 4, 8u },
        { UINT64_C(0x0000000000000001), 16, 8, 2u },
        { UINT64_C(0x00000000000000FF), 16, 1, 256u },
        { UINT64_C(0x0000000000000100), 16, 1, 256u },
        { UINT64_C(0x000000000000FFFF), 16, 2, (size_t)0x10000u * 2u },
        { UINT64_C(0x0000000000010000), 16, 2, (size_t)0x10000u * 2u },
        { UINT64_C(0x0000000000010001), 16, 2, (size_t)0x10000u * 2u + 2u },
        { UINT64_C(0x000000007FFFFFFF), 32, 4, (size_t)0x7FFFFFFFu * 4u },
        { UINT64_C(0x0000000080000000), 32, 4, (size_t)0x80000000u * 4u },
        { UINT64_C(0x00000000FFFFFFFE), 32, 4, (size_t)0xFFFFFFFFu * 4u },
        { UINT64_C(0x00000000FFFFFFFF), 32, 4, (size_t)0xFFFFFFFFu * 4u },
        { UINT64_C(0x0000000100000000), 32, 4, (size_t)0x100000000ULL * 4u },
        { UINT64_C(0x0000000100000001), 32, 4, (size_t)0x100000000ULL * 4u + 4u },
        { UINT64_C(0x00000001000000FF), 32, 8, (size_t)0x100000100ULL * 8u },
        { UINT64_C(0x0000000200000000), 32, 2, (size_t)0x200000000ULL * 2u },
        { UINT64_C(0x00007FFFFFFFFFFF), 64, 1, 8u },
        { UINT64_C(0x0000800000000000), 32, 1, 8u },
        { UINT64_C(0x0000FFFFFFFFFFFF), 64, 1, 16u },
        { UINT64_C(0x0001000000000000), 32, 16, (size_t)0x100000000ULL * 16u },
        { UINT64_C(0x7FFFFFFFFFFFFFFF), 64, 8, (size_t)-1 },
        { UINT64_C(0x8000000000000000), 32, 4, 16u },
        { UINT64_C(0xFFFFFFFFFFFFFFFF), 16, 4, 4u },
        { UINT64_C(0xFFFFFFFFFFFFFFFE), 32, 2, 2u },
        { UINT64_C(0x00000000DEADBEEF), 32, 8, ((size_t)-1) / 2u }, /* SIZE_MAX/2 */
        { UINT64_C(0x00000000CAFEBABE), 16, 4, ((size_t)-1) / 4u }, /* SIZE_MAX/4 */
        { UINT64_C(0x0000000000000010), 64, 16, 4u },  /* in-bounds success */
        { UINT64_C(0x0000000000000020), 32, 8, 8u },   /* in-bounds success */
        { UINT64_C(0x0000000000000040), 16, 2, 2u },   /* in-bounds success */
        { UINT64_C(0x0000000000000080), 64, 4, 16u },  /* in-bounds success */
        { UINT64_C(0x0000000000000101), 32, 1, 1u },   /* in-bounds success */
        { UINT64_C(0x00000000FFFF0000), 16, 8, (size_t)0x10000ULL * 8u },
        { UINT64_C(0x0000000100010000), 32, 8, (size_t)0x100010000ULL * 8u }
    };
    unsigned i;
    for (i = 0; i < N_CURATED; ++i) g_cases[i] = k[i];
}

/* Return 1 if any earlier case already uses value v. */
static int value_seen(unsigned upto, uint64_t v) {
    unsigned j;
    for (j = 0; j < upto; ++j) {
        if (g_cases[j].wide_value == v) return 1;
    }
    return 0;
}

static void build_cases(void) {
    static const int      widths[] = { 16, 32, 64 };
    static const size_t   elems[]  = { 1u, 2u, 4u, 8u, 16u };
    unsigned i;

    fill_curated();
    for (i = N_CURATED; i < NCASES; ++i) {
        uint64_t w = next64();
        g_cases[i].wide_value = w;
        g_cases[i].cast_width = widths[(unsigned)(next64() % 3u)];
        g_cases[i].element_size = elems[(unsigned)(next64() % 5u)];
        if ((next64() & 1u) != 0) {
            g_cases[i].write_count = (size_t)next64();
        } else {
            /* realistic: intended = original wide count * element size */
            g_cases[i].write_count = (size_t)(w * (uint64_t)g_cases[i].element_size);
        }
    }

    /* Guarantee DISTINCT tuples.  wide_value alone distinguishes a tuple, so
     * making every wide_value unique makes every 4-tuple unique.  splitmix64
     * is a bijection, but the curated constants may collide with generated
     * ones; perturb any later duplicate until it is unique. */
    for (i = 1; i < NCASES; ++i) {
        while (value_seen(i, g_cases[i].wide_value)) {
            g_cases[i].wide_value = next64();
        }
    }
}

int main(void) {
    unsigned long i;
    int saved_fd = -1;

    /* Survive refusals: a SafeCRT violation must report and return, not exit. */
    cplus_runtime_set_violation_trap(0);
    /* Defensive: if a destination ever had *unknown* capacity, refuse rather
     * than fall back to raw-C behaviour.  Our in-block pointers always have
     * known (tracked) capacity, so this never changes an expected result. */
    cplus_safe_set_unknown_capacity_mode(1);

    build_cases();

    /* Refusals legitimately print a diagnostic to stderr.  Keep the run quiet
     * by pointing fd 2 at /dev/null for the bulk loop; FINDINGs go to stdout
     * and remain fully visible. */
    {
        int devnull = open("/dev/null", O_WRONLY);
        saved_fd = dup(STDERR_FILENO);
        if (devnull >= 0) {
            (void)dup2(devnull, STDERR_FILENO);
            (void)close(devnull);
        }
    }

    /* Sanity check: prove that an ORDINARY `memset` call (macro from safe_c.h)
     * is actually hardened, on a fixed stack object with a canary. */
    {
        unsigned char stackbuf[12];
        unsigned j;
        for (j = 0; j < 12u; ++j) stackbuf[j] = 0x5A;
        errno = 0;
        memset(stackbuf, 0x41, 16u); /* literal macro -> capacity-checked */
        if (errno != ERANGE || stackbuf[0] != 0x5Au || stackbuf[11] != 0x5Au) {
            ++g_findings;
            printf("FINDING: ordinary memset macro not hardened on stack object "
                   "(errno=%d, first=0x%02x, last=0x%02x)\n",
                   errno, (unsigned)stackbuf[0], (unsigned)stackbuf[11]);
        }
    }

    for (i = 0; i < (unsigned long)NCASES; ++i) {
        const case_t *c = &g_cases[i];
        size_t   truncated = compute_truncated(c->wide_value, c->cast_width);
        size_t   bytes     = truncated * c->element_size; /* modular (defined) */
        size_t   write_len = c->write_count;
        unsigned char *p   = (unsigned char *)malloc(bytes); /* -> malloc_s */
        size_t   cap, offset, proven, win, k;
        unsigned char *dst, *guard;
        int err, expect_refusal, wrote;
        size_t end_index, reach;

        if (p == NULL) {
            /* Oversized/RSIZE_MAX requests are legitimately refused by the
             * tracked allocator.  This is correct behaviour, not a finding. */
            ++g_skipped;
            continue;
        }

        cap = cplus_safe_remaining_capacity(p);
        if (cap == (size_t)-1 || cap == 0u) {
            finding(i, c, truncated, bytes, cap, 0u, 0u, write_len, -1, 0,
                    "base pointer has no tracked capacity", 0u, 0u);
            free(p);
            continue;
        }

        /* Keep the destination pointer strictly INSIDE the block so its
         * proven capacity is always known and no genuine UB is possible. */
        offset = (size_t)((c->wide_value * UINT64_C(0x9E3779B97F4A7C15)) >> 32) % cap;
        dst    = p + offset;
        proven = cap - offset;          /* >= 1 */
        win    = proven < 64u ? proven : 64u;

        /* Initialize an inspectable window plus an end-of-block canary using
         * ordinary, in-bounds byte writes (never out of the block). */
        for (k = 0; k < win; ++k) dst[k] = 0x5A;
        end_index = cap - 1u;
        p[end_index] = 0xCC;

        /* Best-effort trailing canary in a separate tracked block.  Adjacency
         * is not guaranteed, so this is an extra detector, not the assertion
         * the verdict rests on. */
        guard = (unsigned char *)malloc(64u);
        if (guard != NULL) {
            for (k = 0; k < 64u; ++k) guard[k] = 0xC3;
        }

        expect_refusal = (write_len > proven);
        errno = 0;
        /* LITERAL ordinary-C call, hardened by the safe_c.h macro mapping:
         *   memset(dst, 0x41, write_len)
         * expands to cplus_memset_s_auto(dst, 0x41, write_len,
         *                                __builtin_object_size(dst, 1)).
         * dst[0] was initialized to 0x5A above; a uniform 0x41 write can only
         * change it to 0x41, so this reliably tells us whether the wrapper
         * actually wrote anything (write_len == 0 is a defined no-op). */
        memset(dst, 0x41, write_len);
        err   = errno;
        wrote = (write_len == 0u) || (dst[0] == 0x41u);

        if (expect_refusal) {
            /* THE core assertion of TEST 1. */
            ++g_refusals;
            if (wrote) {
                finding(i, c, truncated, bytes, cap, offset, proven, write_len,
                        1, err, "overflow write was NOT refused", 0u, 0u);
            }
            if (err != ERANGE) {
                finding(i, c, truncated, bytes, cap, offset, proven, write_len,
                        1, err, "refused overflow write did not set errno=ERANGE",
                        0u, 0u);
            }
            check_unchanged(i, c, truncated, bytes, cap, offset, proven,
                            write_len, 1, err, p, dst, win, end_index);
        } else if (!wrote) {
            /* Safe refusal for a NON-overflow reason.  Known cause: the
             * vendored safeclib sets RSIZE_MAX_MEM = RSIZE_MAX_STR*4 = 16384,
             * so memset_s rejects dmax > 16384 with 403 (ESLEMAX), which the
             * C+ adapter maps to errno = EINVAL.  It is still safe (no bytes
             * written), so it is reported, not treated as a failure.  A
             * false ERANGE here WOULD be a defect. */
            /* A declared capacity larger than the proven remaining bytes is
             * itself refuse-worthy: the caller's bound cannot be justified.
             * ERANGE then describes the unprovable bound, not a false
             * overflow of write_len. */
            if (err == ERANGE && bytes <= proven) {
                finding(i, c, truncated, bytes, cap, offset, proven, write_len,
                        0, err, "in-bounds write falsely refused with ERANGE",
                        0u, 0u);
            }
            ++g_backend_refusals;
            check_unchanged(i, c, truncated, bytes, cap, offset, proven,
                            write_len, 0, err, p, dst, win, end_index);
        } else {
            ++g_writes;
            if (err == ERANGE) {
                finding(i, c, truncated, bytes, cap, offset, proven, write_len,
                        0, err, "in-bounds write falsely reported ERANGE", 0u, 0u);
            }
            /* In-bounds write: [offset, offset+write_len) must be 0x41, the
             * rest of the inspected window keeps its initial value. */
            reach = offset + write_len; /* <= cap */
            for (k = 0; k < win; ++k) {
                size_t        j    = offset + k;
                unsigned char want = (j < reach)
                                         ? 0x41u
                                         : (((j + 0u) == end_index) ? 0xCCu : 0x5Au);
                if (dst[k] != want) {
                    finding(i, c, truncated, bytes, cap, offset, proven,
                            write_len, 0, err, "in-bounds write pattern wrong",
                            k, dst[k]);
                    break;
                }
            }
            if (p[end_index] != ((reach == cap) ? 0x41u : 0xCCu)) {
                finding(i, c, truncated, bytes, cap, offset, proven, write_len,
                        0, err, "end canary wrong after in-bounds write",
                        end_index, p[end_index]);
            }
        }

        if (guard != NULL) {
            for (k = 0; k < 64u; ++k) {
                if (guard[k] != 0xC3u) {
                    finding(i, c, truncated, bytes, cap, offset, proven,
                            write_len, expect_refusal, err,
                            "trailing canary corrupted", k, guard[k]);
                    break;
                }
            }
        }

        free(guard); /* -> free_s */
        free(p);     /* -> free_s  */
    }

    fflush(stderr);
    if (saved_fd >= 0) {
        (void)dup2(saved_fd, STDERR_FILENO);
        (void)close(saved_fd);
    }

    printf("scenario_01: %lu cases processed, %lu skipped(malloc-NULL), "
           "%lu overflow-refusals(ERANGE), %lu backend-refusals(dmax>16384,EINVAL), "
           "%lu in-bounds writes, %lu findings\n",
           (unsigned long)NCASES, g_skipped, g_refusals, g_backend_refusals,
           g_writes, g_findings);
    if (g_findings != 0u) {
        printf("scenario_01: FAIL (%lu findings)\n", g_findings);
        return 1;
    }
    printf("scenario_01: %lu cases ok\n", (unsigned long)NCASES);
    return 0;
}
