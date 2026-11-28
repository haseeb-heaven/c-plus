/* Adversarial checked-memory scenarios (M-A, file 4/10).
 *
 * Topic: cplus_memset_s bounds and value handling: exact/partial fills, n > dmax
 * (partial value write, ESNOSPC), dmax == 0, NULL dest (including n == 0),
 * value > 255, dmax/n > RSIZE_MAX_MEM, and guard bytes after success/rejection.
 *
 * Buffer is TOTAL bytes; DMAX is a truthful sub-bound. Report-only.
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
    unsigned char buf[TOTAL];
    size_t i;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* (1) exact fill n == dmax, guard past dmax intact */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0x7E, DMAX) == 0);
    CHECK(all_eq(buf, 0, DMAX, 0x7E));
    CHECK(all_eq(buf, DMAX, TOTAL, 0x5A));

    /* (2) partial fill n < dmax leaves the tail untouched */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0x01, 3) == 0);
    CHECK(all_eq(buf, 0, 3, 0x01));
    CHECK(all_eq(buf, 3, TOTAL, 0x5A));

    /* (3) n == 0 is a no-op success, destination untouched */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0x99, 0) == 0);
    CHECK(all_eq(buf, 0, TOTAL, 0x5A));

    /* (4) n > dmax -> ESNOSPC, partial write of dmax bytes with the value */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0x5C, DMAX + 4) == ESNOSPC);
    CHECK(all_eq(buf, 0, DMAX, 0x5C));       /* written prefix uses value */
    CHECK(all_eq(buf, DMAX, TOTAL, 0x5A));   /* guard intact */

    /* (5) dmax == 0 with n > 0 -> ESNOSPC, nothing written */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, 0, 0x22, 1) == ESNOSPC);
    CHECK(all_eq(buf, 0, TOTAL, 0x5A));

    /* (6) dmax == 0 with n == 0 -> EOK */
    CHECK(cplus_memset_s(buf, 0, 0x22, 0) == 0);

    /* (7) NULL dest is rejected even when n == 0 */
    CHECK(cplus_memset_s(NULL, DMAX, 0x00, 0) == ESNULLP);
    CHECK(cplus_memset_s(NULL, DMAX, 0x00, 1) == ESNULLP);

    /* (8) value > 255 -> ESLEMAX, destination untouched */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 256, 1) == ESLEMAX);
    CHECK(all_eq(buf, 0, TOTAL, 0x5A));

    /* (9) value boundary values 0 and 255 are valid */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0, DMAX) == 0);
    CHECK(all_eq(buf, 0, DMAX, 0x00));
    memset(buf, 0x00, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 255, DMAX) == 0);
    CHECK(all_eq(buf, 0, DMAX, 0xFF));

    /* (10) value > 255 is checked before n > dmax: still ESLEMAX, no write */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 256, DMAX + 4) == ESLEMAX);
    CHECK(all_eq(buf, 0, TOTAL, 0x5A));

    /* (11) dmax > RSIZE_MAX_MEM -> ESLEMAX, no write */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, (rsize_t)RSIZE_MAX_MEM + 1, 0x11, 1) == ESLEMAX);
    CHECK(all_eq(buf, 0, TOTAL, 0x5A));

    /* (12) dmax > RSIZE_MAX_MEM with n == 0 short-circuits to EOK */
    CHECK(cplus_memset_s(buf, (rsize_t)RSIZE_MAX_MEM + 1, 0x11, 0) == 0);

    /* (13) n > RSIZE_MAX_MEM (valid dmax) -> ESLEMAX, partial value write */
    memset(buf, 0x5A, sizeof buf);
    CHECK(cplus_memset_s(buf, DMAX, 0x11, (rsize_t)RSIZE_MAX_MEM + 1) == ESLEMAX);
    CHECK(all_eq(buf, 0, DMAX, 0x11));
    CHECK(all_eq(buf, DMAX, TOTAL, 0x5A));

    /* (14) looping identical fills stays deterministic */
    for (i = 0; i < 100; i++) {
        memset(buf, 0x5A, sizeof buf);
        CHECK(cplus_memset_s(buf, DMAX, 0xA5, DMAX) == 0);
        CHECK(all_eq(buf, 0, DMAX, 0xA5));
        CHECK(all_eq(buf, DMAX, TOTAL, 0x5A));
    }

    if (failures == 0) {
        printf("test_mem50_memset_bounds: ok\n");
        return 0;
    }
    printf("test_mem50_memset_bounds: FAILURES=%d\n", failures);
    return 1;
}
