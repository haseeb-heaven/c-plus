/* Adversarial checked-memory scenarios (M-A, file 9/10).
 *
 * Topic: repeated / looped stress. Every case runs 100 iterations to show the
 * checked memory APIs behave deterministically across many calls: valid copies
 * of all lengths, one-past-fit rejection with guard preservation, overlap
 * (memmove succeeds, memcpy rejected), memcmp signs, memset, memzero.
 *
 * Buffers are TOTAL bytes; DMAX is a truthful sub-bound. Report-only.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_log.h"
#include "safe_error.h"
#include "safe_lib_errno.h"

#define DMAX  12
#define TOTAL 20

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
    unsigned char src[TOTAL], dst[TOTAL], b[TOTAL], buf[TOTAL];
    int diff;
    size_t i, iter;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (i = 0; i < TOTAL; i++) src[i] = (unsigned char)(i + 1);

    /* (1) 100x: every valid copy length 0..DMAX succeeds and is correct */
    for (iter = 0; iter < 100; iter++) {
        for (i = 0; i <= (size_t)DMAX; i++) {
            memset(dst, 0xAA, sizeof dst);
            CHECK(cplus_memcpy_s(dst, DMAX, src, (rsize_t)i) == 0);
            CHECK(memcmp(dst, src, i) == 0);
        }
    }

    /* (2) 100x: one-past-fit rejected, prefix zeroed, guard intact */
    for (iter = 0; iter < 100; iter++) {
        memset(dst, 0x5A, sizeof dst);
        CHECK(cplus_memcpy_s(dst, DMAX, src, DMAX + 1) == ESNOSPC);
        CHECK(all_eq(dst, 0, DMAX, 0x00));
        CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));
    }

    /* (3) 100x: memmove overlap forward and backward both correct */
    for (iter = 0; iter < 100; iter++) {
        memcpy(b, "ABCDEFGHIJKL", DMAX);
        CHECK(cplus_memmove_s(b, DMAX - 2, b + 2, DMAX - 2) == 0);
        CHECK(memcmp(b, "CDEFGHIJKL", 10) == 0);
        memcpy(b, "ABCDEFGHIJKL", DMAX);
        CHECK(cplus_memmove_s(b + 2, DMAX - 2, b, DMAX - 2) == 0);
        CHECK(memcmp(b, "ABABCDEFGHIJ", 12) == 0);
    }

    /* (4) 100x: the same overlap is rejected by memcpy_s */
    for (iter = 0; iter < 100; iter++) {
        memcpy(b, "ABCDEFGHIJKL", DMAX);
        CHECK(cplus_memcpy_s(b, DMAX - 2, b + 2, DMAX - 2) == ESOVRLP);
        CHECK(all_eq(b, 0, DMAX - 2, 0x00));
    }

    /* (5) 100x: memcmp sign for equal / less / greater */
    for (iter = 0; iter < 100; iter++) {
        CHECK(cplus_memcmp_s("hello", 6, "hello", 5, &diff) == 0 && diff == 0);
        CHECK(cplus_memcmp_s("apple", 6, "apply", 5, &diff) == 0 && diff == -1);
        CHECK(cplus_memcmp_s("apply", 6, "apple", 5, &diff) == 0 && diff == 1);
    }

    /* (6) 100x: memset exact fill and guard preservation */
    for (iter = 0; iter < 100; iter++) {
        memset(buf, 0x5A, sizeof buf);
        CHECK(cplus_memset_s(buf, DMAX, 0x6F, DMAX) == 0);
        CHECK(all_eq(buf, 0, DMAX, 0x6F));
        CHECK(all_eq(buf, DMAX, TOTAL, 0x5A));
    }

    /* (7) 100x: memzero bounded zeroing, guard preservation */
    for (iter = 0; iter < 100; iter++) {
        memset(buf, 0xEE, sizeof buf);
        CHECK(cplus_memzero_s(buf, DMAX) == 0);
        CHECK(all_eq(buf, 0, DMAX, 0x00));
        CHECK(all_eq(buf, DMAX, TOTAL, 0xEE));
    }

    /* (8) 100x: copy then compare round-trip with identical pointers */
    for (iter = 0; iter < 100; iter++) {
        CHECK(cplus_memcpy_s(dst, DMAX, src, DMAX) == 0);
        CHECK(cplus_memcmp_s(dst, DMAX, src, DMAX, &diff) == 0 && diff == 0);
        CHECK(cplus_memcpy_s(dst, DMAX, dst, DMAX) == 0);
    }

    if (failures == 0) {
        printf("test_mem50_stress_loops: ok\n");
        return 0;
    }
    printf("test_mem50_stress_loops: FAILURES=%d\n", failures);
    return 1;
}
