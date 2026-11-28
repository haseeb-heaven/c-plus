/* Adversarial checked-memory scenarios (M-A, file 7/10).
 *
 * Topic: guard-byte integrity across every checked memory API, both after a
 * successful operation and after a rejection. Leading and trailing guard
 * regions around an interior destination window are verified with a non-zero
 * fill pattern.
 *
 * Buffer is TOTAL bytes; LEN is a truthful bound for the interior window.
 * Report-only.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_log.h"
#include "safe_error.h"
#include "safe_lib_errno.h"

#define TOTAL 16
#define LEN   8
#define GUARD 0xAB

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
    size_t i;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (i = 0; i < TOTAL; i++) src[i] = (unsigned char)(0x60 + i);

    /* (1) memcpy_s success: trailing guard [LEN, TOTAL) intact */
    memset(buf, GUARD, sizeof buf);
    CHECK(cplus_memcpy_s(buf, LEN, src, LEN) == 0);
    CHECK(memcmp(buf, src, LEN) == 0);
    CHECK(all_eq(buf, LEN, TOTAL, GUARD));

    /* (2) memcpy_s one-past rejection: guard and prefix left intact */
    memset(buf, GUARD, sizeof buf);
    CHECK(cplus_memcpy_s(buf, LEN, src, LEN + 1) == ESNOSPC);
    CHECK(all_eq(buf, 0, LEN, GUARD));
    CHECK(all_eq(buf, LEN, TOTAL, GUARD));

    /* (3) memcpy_s NULL-src rejection: guard intact */
    memset(buf, GUARD, sizeof buf);
    CHECK(cplus_memcpy_s(buf, LEN, NULL, 4) == ESNULLP);
    CHECK(all_eq(buf, LEN, TOTAL, GUARD));

    /* (4) memcpy_s overlap rejection: guard intact */
    memset(buf, GUARD, sizeof buf);
    memcpy(buf, "ABCDEFGH", LEN);
    CHECK(cplus_memcpy_s(buf, LEN - 2, buf + 2, LEN - 2) == ESOVRLP);
    CHECK(all_eq(buf, LEN, TOTAL, GUARD));

    /* (5) memmove_s success (overlap) within window: guard intact */
    memset(buf, GUARD, sizeof buf);
    memcpy(buf, "ABCDEFGH", LEN);
    CHECK(cplus_memmove_s(buf, LEN - 2, buf + 2, LEN - 2) == 0);
    CHECK(all_eq(buf, LEN, TOTAL, GUARD));

    /* (6) memmove_s NULL-src and one-past rejections: guard intact */
    memset(buf, GUARD, sizeof buf);
    CHECK(cplus_memmove_s(buf, LEN, NULL, 1) == ESNULLP);
    CHECK(cplus_memmove_s(buf, LEN, src, LEN + 1) == ESNOSPC);
    CHECK(all_eq(buf, LEN, TOTAL, GUARD));

    /* (7) memset_s success and n > dmax partial write: guard intact */
    memset(buf, GUARD, sizeof buf);
    CHECK(cplus_memset_s(buf, LEN, 0x11, LEN) == 0);
    CHECK(all_eq(buf, 0, LEN, 0x11));
    CHECK(all_eq(buf, LEN, TOTAL, GUARD));
    memset(buf, GUARD, sizeof buf);
    CHECK(cplus_memset_s(buf, LEN, 0x22, LEN + 5) == ESNOSPC);
    CHECK(all_eq(buf, LEN, TOTAL, GUARD));

    /* (8) memzero_s success and zero-length rejection: guard intact */
    memset(buf, 0xFF, sizeof buf);
    CHECK(cplus_memzero_s(buf, LEN) == 0);
    CHECK(all_eq(buf, 0, LEN, 0x00));
    CHECK(all_eq(buf, LEN, TOTAL, 0xFF));    /* trailing guard was 0xFF */
    memset(buf, GUARD, sizeof buf);
    CHECK(cplus_memzero_s(buf, 0) == ESZEROL);
    CHECK(all_eq(buf, 0, TOTAL, GUARD));

    /* (9) interior window with leading AND trailing guards: success */
    memset(buf, GUARD, sizeof buf);
    memcpy(buf + 4, "12345678", 8);          /* payload occupies [4,12) */
    CHECK(cplus_memcpy_s(buf + 4, LEN, src, LEN) == 0);
    CHECK(memcmp(buf + 4, src, LEN) == 0);
    CHECK(all_eq(buf, 0, 4, GUARD));
    CHECK(all_eq(buf, 12, TOTAL, GUARD));

    /* (10) interior window with leading AND trailing guards: rejection */
    memset(buf, GUARD, sizeof buf);
    CHECK(cplus_memcpy_s(buf + 4, LEN, NULL, 1) == ESNULLP);
    CHECK(all_eq(buf, 0, 4, GUARD));         /* leading guard intact */
    CHECK(all_eq(buf, 12, TOTAL, GUARD));    /* trailing guard intact */
    CHECK(all_eq(buf, 4, 12, GUARD));        /* interior prefix intact */

    if (failures == 0) {
        printf("test_mem50_guard_bytes: ok\n");
        return 0;
    }
    printf("test_mem50_guard_bytes: FAILURES=%d\n", failures);
    return 1;
}
