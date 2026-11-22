/* Adversarial checked-memory scenarios (M-A, file 10/10).
 *
 * Topic: cross-API interaction. Copies validated with memcmp, memmove shifts,
 * memset followed by memzero, identical source/destination pointers for every
 * API, overlap rejection versus memmove recovery, one-past rejection followed by
 * a successful exact copy, guard integrity across a sequence, and the
 * RSIZE_MAX_MEM boundary for every checked memory function.
 *
 * Buffers are TOTAL bytes; DMAX is a truthful sub-bound. Report-only.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_log.h"
#include "safe_error.h"
#include "safe_lib_errno.h"

#define DMAX  8
#define TOTAL 16

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

static int all_eq(const unsigned char *p, size_t from, size_t to,
                  unsigned char v) {
    size_t k;
    for (k = from; k < to; k++)
        if (p[k] != v) return 0;
    return 1;
}

int main(void) {
    unsigned char buf[TOTAL], src[TOTAL];
    int diff;
    size_t i;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (i = 0; i < TOTAL; i++) src[i] = (unsigned char)(0x90 + i);

    /* (1) memcpy_s result verified with memcmp_s (equal) */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memcpy_s(buf, DMAX, src, DMAX) == 0);
    CHECK(cplus_memcmp_s(buf, DMAX, src, DMAX, &diff) == 0);
    CHECK(diff == 0);
    CHECK(buf[DMAX] == 0x5A);                /* guard intact */

    /* (2) memmove_s shift verified against the expected block */
    memset(buf, 0x5A, sizeof buf);
    memcpy(buf, "ABCDEFGH", DMAX);
    CHECK(cplus_memmove_s(buf + 2, DMAX - 2, buf, DMAX - 2) == 0);
    CHECK(memcmp(buf, "ABABCDEF", DMAX) == 0);
    CHECK(buf[DMAX] == 0x5A);

    /* (3) memset_s then memzero_s: full window zeroed, guard untouched */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0xAA, DMAX) == 0);
    CHECK(all_eq(buf, 0, DMAX, 0xAA));
    CHECK(cplus_memzero_s(buf, DMAX) == 0);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    CHECK(all_eq(buf, DMAX, TOTAL, 0x5A));

    /* (4) identical pointers accepted by every API */
    memcpy(buf, "IDENTICAL", 9);
    CHECK(cplus_memcpy_s(buf, DMAX, buf, DMAX) == 0);
    CHECK(cplus_memmove_s(buf, DMAX, buf, DMAX) == 0);
    CHECK(cplus_memcmp_s(buf, TOTAL, buf, TOTAL, &diff) == 0 && diff == 0);
    CHECK(cplus_memset_s(buf, DMAX, 0x00, DMAX) == 0);
    CHECK(cplus_memzero_s(buf, DMAX) == 0);
    CHECK(all_eq(buf, 0, DMAX, 0x00));

    /* (5) overlap: memcpy rejected, memmove recovers the same data */
    memcpy(buf, "ABCDEFGH", DMAX);
    CHECK(cplus_memcpy_s(buf, DMAX - 2, buf + 2, DMAX - 2) == ESOVRLP);
    CHECK(all_eq(buf, 0, DMAX - 2, 0x00));   /* prefix cleared by rejection */
    memcpy(buf, "ABCDEFGH", DMAX);
    CHECK(cplus_memmove_s(buf, DMAX - 2, buf + 2, DMAX - 2) == 0);
    CHECK(memcmp(buf, "CDEFGH", DMAX - 2) == 0);

    /* (6) one-past rejection then a successful exact copy */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memcpy_s(buf, DMAX, src, DMAX + 1) == ESNOSPC);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    CHECK(cplus_memcpy_s(buf, DMAX, src, DMAX) == 0);
    CHECK(memcmp(buf, src, DMAX) == 0);
    CHECK(buf[DMAX] == 0x5A);

    /* (7) guard integrity across a sequence of mixed operations */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0x11, DMAX) == 0);
    CHECK(cplus_memcpy_s(buf, DMAX, src, DMAX) == 0);
    CHECK(cplus_memmove_s(buf, DMAX, buf + 0, DMAX) == 0);
    CHECK(cplus_memzero_s(buf, DMAX) == 0);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    CHECK(all_eq(buf, DMAX, TOTAL, 0x5A));

    /* (8) RSIZE_MAX_MEM boundary for every checked memory function */
    CHECK(cplus_memcpy_s(buf, (rsize_t)RSIZE_MAX_MEM + 1, src, 1) == ESLEMAX);
    CHECK(cplus_memmove_s(buf, (rsize_t)RSIZE_MAX_MEM + 1, src, 1) == ESLEMAX);
    CHECK(cplus_memset_s(buf, (rsize_t)RSIZE_MAX_MEM + 1, 0, 1) == ESLEMAX);
    CHECK(cplus_memzero_s(buf, (rsize_t)RSIZE_MAX_MEM + 1) == ESLEMAX);
    CHECK(cplus_memcmp_s(src, (rsize_t)RSIZE_MAX_MEM + 1, src, 1, &diff) == ESLEMAX);

    if (failures == 0) {
        printf("test_mem50_cross_api: ok\n");
        return 0;
    }
    printf("test_mem50_cross_api: FAILURES=%d\n", failures);
    return 1;
}
