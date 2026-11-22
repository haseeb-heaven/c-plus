/* Adversarial checked-memory scenarios (M-A, file 2/10).
 *
 * Topic: cplus_memcpy_s boundary rejection: NULL dest/src, zero dmax (with
 * zero and non-zero lengths), slen > dmax, dmax/slen > RSIZE_MAX_MEM, plus the
 * deterministic zeroed destination prefix and guard bytes after a rejection.
 *
 * Dest buffer is TOTAL bytes; DMAX is a truthful sub-bound. Report-only.
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
    for (i = 0; i < TOTAL; i++) src[i] = (unsigned char)(0x20 + i);

    /* (1) NULL src with a non-zero length -> ESNULLP, prefix cleared */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, NULL, 4) == ESNULLP);
    CHECK(all_eq(dst, 0, DMAX, 0x00));       /* rejected prefix zeroed */
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));   /* guard past dmax intact */

    /* (2) NULL dest with a non-zero length -> ESNULLP */
    CHECK(cplus_memcpy_s(NULL, DMAX, src, 4) == ESNULLP);

    /* (3) NULL src but zero length -> EOK (length 0 short-circuits) */
    CHECK(cplus_memcpy_s(dst, DMAX, NULL, 0) == 0);

    /* (4) NULL dest, zero dmax, zero length -> EOK */
    CHECK(cplus_memcpy_s(NULL, 0, NULL, 0) == 0);

    /* (5) zero dmax with non-zero length -> ESZEROL, dest untouched */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, 0, src, 1) == ESZEROL);
    CHECK(all_eq(dst, 0, TOTAL, 0x5A));

    /* (6) zero dmax with zero length -> EOK */
    CHECK(cplus_memcpy_s(dst, 0, src, 0) == 0);

    /* (7) slen > dmax -> ESNOSPC; prefix zeroed, guard intact */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, src, DMAX + 1) == ESNOSPC);
    CHECK(all_eq(dst, 0, DMAX, 0x00));
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));

    /* (8) slen much larger than dmax -> still ESNOSPC */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, src, DMAX * 100) == ESNOSPC);
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));

    /* (9) dmax > RSIZE_MAX_MEM is rejected before any use of dmax */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, (rsize_t)RSIZE_MAX_MEM + 1, src, 1) == ESLEMAX);
    CHECK(all_eq(dst, 0, TOTAL, 0x5A));      /* dest untouched: no prefix clear */

    /* (10) slen > RSIZE_MAX_MEM (with valid dmax) -> ESLEMAX, prefix cleared */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, src, (rsize_t)RSIZE_MAX_MEM + 1) == ESLEMAX);
    CHECK(all_eq(dst, 0, DMAX, 0x00));
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));

    /* (11) slen == the public header's RSIZE_MAX_MEM. The public header
     * advertises RSIZE_MAX_MEM = SIZE_MAX>>1, but the vendored safeclib backend
     * enforces a much smaller per-call limit (RSIZE_MAX_STR * 4, i.e. 16384).
     * Either way an oversized slen maps to ESLEMAX, and the destination prefix
     * is cleared. Anomaly reported separately; library code is not changed. */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, src, (rsize_t)RSIZE_MAX_MEM) == ESLEMAX);
    CHECK(all_eq(dst, 0, DMAX, 0x00));

    /* (12) repeated identical rejection is deterministic */
    memset(dst, 0x5A, sizeof dst);
    CHECK(cplus_memcpy_s(dst, DMAX, NULL, 4) == ESNULLP);
    CHECK(cplus_memcpy_s(dst, DMAX, NULL, 4) == ESNULLP);
    CHECK(all_eq(dst, 0, DMAX, 0x00));
    CHECK(all_eq(dst, DMAX, TOTAL, 0x5A));

    if (failures == 0) {
        printf("test_mem50_memcpy_bounds: ok\n");
        return 0;
    }
    printf("test_mem50_memcpy_bounds: FAILURES=%d\n", failures);
    return 1;
}
