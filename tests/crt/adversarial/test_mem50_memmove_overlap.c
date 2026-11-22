/* Adversarial checked-memory scenarios (M-A, file 3/10).
 *
 * Topic: cplus_memmove_s offset overlap in both directions versus the cplus_memcpy_s
 * overlap rejection, identical source/destination pointers, non-overlapping
 * adjacent moves, and the usual null/zero/one-past guards.
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
    unsigned char src[TOTAL], dst[TOTAL];
    unsigned char b[TOTAL];
    size_t i;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (i = 0; i < TOTAL; i++) src[i] = (unsigned char)(0x40 + i);

    /* (1) forward overlap (dest < src): memmove_s shifts correctly */
    memset(b, 0x5A, sizeof b);
    memcpy(b, "ABCDEFGH", 8);
    CHECK(cplus_memmove_s(b, 6, b + 2, 6) == 0);
    CHECK(memcmp(b, "CDEFGH", 6) == 0);
    CHECK(b[6] == 'G');
    CHECK(b[7] == 'H');
    CHECK(all_eq(b, 8, TOTAL, 0x5A));

    /* (2) the very same forward overlap is rejected by memcpy_s */
    memset(b, 0x5A, sizeof b);
    memcpy(b, "ABCDEFGH", 8);
    CHECK(cplus_memcpy_s(b, 6, b + 2, 6) == ESOVRLP);
    CHECK(all_eq(b, 0, 6, 0x00));            /* rejected prefix zeroed */
    CHECK(all_eq(b, 8, TOTAL, 0x5A));

    /* (3) backward overlap (dest > src): memmove_s shifts correctly */
    memset(b, 0x5A, sizeof b);
    memcpy(b, "ABCDEFGH", 8);
    CHECK(cplus_memmove_s(b + 2, 6, b, 6) == 0);
    CHECK(memcmp(b, "ABABCDEF", 8) == 0);
    CHECK(all_eq(b, 8, TOTAL, 0x5A));

    /* (4) the same backward overlap is rejected by memcpy_s */
    memset(b, 0x5A, sizeof b);
    memcpy(b, "ABCDEFGH", 8);
    CHECK(cplus_memcpy_s(b + 2, 6, b, 6) == ESOVRLP);
    CHECK(b[0] == 'A' && b[1] == 'B');       /* bytes before dest intact */
    CHECK(all_eq(b, 2, 8, 0x00));            /* [dest, dest+dmax) zeroed */
    CHECK(all_eq(b, 8, TOTAL, 0x5A));

    /* (5) single-byte partial overlap just past an edge is still overlap */
    memset(b, 0x5A, sizeof b);
    memcpy(b, "ABCDEFGH", 8);
    CHECK(cplus_memcpy_s(b + 1, 7, b, 7) == ESOVRLP);

    /* (6) non-overlapping adjacent move succeeds with memcpy_s */
    memset(b, 0x5A, sizeof b);
    memcpy(b, "abcd", 4);
    CHECK(cplus_memcpy_s(b + 8, 4, b, 4) == 0);
    CHECK(memcmp(b + 8, "abcd", 4) == 0);
    CHECK(all_eq(b, 4, 8, 0x5A));

    /* (7) identical pointers are allowed by both APIs */
    memcpy(b, "IDENTICAL", 9);
    CHECK(cplus_memmove_s(b, DMAX, b, DMAX) == 0);
    CHECK(memcmp(b, "IDENTICAL", 9) == 0);
    CHECK(cplus_memcpy_s(b, DMAX, b, DMAX) == 0);
    CHECK(memcmp(b, "IDENTICAL", 9) == 0);

    /* (8) memmove_s boundary rejections mirror memcpy_s */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memmove_s(NULL, DMAX, src, 1) == ESNULLP);
    CHECK(cplus_memmove_s(dst, DMAX, NULL, 1) == ESNULLP);
    CHECK(all_eq(dst, 0, DMAX, 0x00));       /* null src clears prefix */
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));
    CHECK(cplus_memmove_s(dst, 0, src, 1) == ESZEROL);
    CHECK(cplus_memmove_s(dst, 0, src, 0) == 0);
    CHECK(cplus_memmove_s(dst, DMAX, src, DMAX + 1) == ESNOSPC);
    CHECK(cplus_memmove_s(dst, (rsize_t)RSIZE_MAX_MEM + 1, src, 1) == ESLEMAX);
    CHECK(cplus_memmove_s(dst, DMAX, src, (rsize_t)RSIZE_MAX_MEM + 1) == ESLEMAX);
    CHECK(cplus_memmove_s(dst, DMAX, src, 0) == 0);

    if (failures == 0) {
        printf("test_mem50_memmove_overlap: ok\n");
        return 0;
    }
    printf("test_mem50_memmove_overlap: FAILURES=%d\n", failures);
    return 1;
}
