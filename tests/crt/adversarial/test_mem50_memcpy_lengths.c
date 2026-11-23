/* Adversarial checked-memory scenarios (M-A, file 1/10).
 *
 * Topic: cplus_memcpy_s valid copy lengths 0..dmax, exact-fit, one-past-fit
 * rejection, and guard bytes past dmax intact after success.
 *
 * The destination buffer is TOTAL bytes; DMAX is a truthful sub-bound
 * (DMAX <= sizeof dst) and bytes [DMAX, TOTAL) are the guard region. This
 * file reports only; it does not change any library code.
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
    size_t i;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (i = 0; i < TOTAL; i++) src[i] = (unsigned char)(i + 1);

    /* (1) every valid length 0..DMAX succeeds and writes exactly that many */
    for (i = 0; i <= (size_t)DMAX; i++) {
        memset(dst, 0xAA, sizeof dst);
        CHECK(cplus_memcpy_s(dst, DMAX, src, (rsize_t)i) == 0);
        CHECK(memcmp(dst, src, i) == 0);
        CHECK(all_eq(dst, i, TOTAL, 0xAA));   /* nothing past len touched */
    }

    /* (2) exact fit: slen == dmax */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, src, DMAX) == 0);
    CHECK(memcmp(dst, src, DMAX) == 0);
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));    /* guard past dmax intact */

    /* (3) one-past-fit is rejected; destination left intact */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, src, DMAX + 1) == ESNOSPC);
    CHECK(all_eq(dst, 0, DMAX, 0x5A));
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));    /* guard past dmax intact */

    /* (4) far-past-fit (a full buffer length) is also rejected */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, src, TOTAL) == ESNOSPC);
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));

    /* (5) single-byte copy at each in-bounds destination offset */
    for (i = 0; i < (size_t)DMAX; i++) {
        memset(dst, 0x11, sizeof dst);
        CHECK(cplus_memcpy_s(dst + i, DMAX - i, src, 1) == 0);
        CHECK(dst[i] == src[0]);
        CHECK(all_eq(dst, 0, i, 0x11));       /* bytes before dest intact */
        CHECK(dst[TOTAL - 1] == 0x11);
    }

    /* (6) exact-fit copy into an interior window leaves both flanks intact */
    memset(dst, 0x77, sizeof dst);
    CHECK(cplus_memcpy_s(dst + 3, 5, src, 5) == 0);
    CHECK(memcmp(dst + 3, src, 5) == 0);
    CHECK(all_eq(dst, 0, 3, 0x77));
    CHECK(all_eq(dst, 8, TOTAL, 0x77));

    /* (7) zero length is a no-op success; destination untouched */
    memset(dst, 0x3C, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, src, 0) == 0);
    CHECK(all_eq(dst, 0, TOTAL, 0x3C));

    if (failures == 0) {
        printf("test_mem50_memcpy_lengths: ok\n");
        return 0;
    }
    printf("test_mem50_memcpy_lengths: FAILURES=%d\n", failures);
    return 1;
}
