/* Adversarial checked-memory scenarios (M-A, file 5/10).
 *
 * Topic: cplus_memcmp_s difference signs (equal / less / greater / prefix),
 * identical pointers, NULL diff, zero dmax/slen, slen > dmax, oversized
 * dmax/slen, and the deterministic default diff (-1) on rejection.
 *
 * Buffers are truthful; report-only.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_log.h"
#include "safe_error.h"
#include "safe_lib_errno.h"

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

int main(void) {
    unsigned char a[TOTAL], b[TOTAL];
    int diff;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    memset(a, 0x33, sizeof a);
    memset(b, 0x33, sizeof b);

    /* (1) equal blocks -> EOK and diff == 0 */
    diff = 99;
    CHECK(cplus_memcmp_s("abc", 4, "abc", 3, &diff) == 0);
    CHECK(diff == 0);

    /* (2) dest < src -> diff == -1 */
    diff = 99;
    CHECK(cplus_memcmp_s("abc", 4, "abd", 3, &diff) == 0);
    CHECK(diff == -1);

    /* (3) dest > src -> diff == +1 */
    diff = 99;
    CHECK(cplus_memcmp_s("abd", 4, "abc", 3, &diff) == 0);
    CHECK(diff == 1);

    /* (4) prefix: first slen bytes equal -> diff == 0 even if dest is longer */
    diff = 99;
    CHECK(cplus_memcmp_s("abcd", 4, "abc", 3, &diff) == 0);
    CHECK(diff == 0);

    /* (5) difference beyond slen is not compared (still diff == 0) */
    diff = 99;
    CHECK(cplus_memcmp_s("abXdef", 6, "abY", 2, &diff) == 0);
    CHECK(diff == 0);

    /* (6) identical pointers short-circuit to diff == 0 */
    diff = 99;
    CHECK(cplus_memcmp_s(a, TOTAL, a, TOTAL, &diff) == 0);
    CHECK(diff == 0);

    /* (7) NULL diff -> ESNULLP */
    CHECK(cplus_memcmp_s(a, TOTAL, b, TOTAL, NULL) == ESNULLP);

    /* (8) slen == 0 -> ESZEROL, diff set to the default -1 */
    diff = 99;
    CHECK(cplus_memcmp_s(a, TOTAL, b, 0, &diff) == ESZEROL);
    CHECK(diff == -1);

    /* (9) dmax == 0 -> ESZEROL, diff default -1 */
    diff = 99;
    CHECK(cplus_memcmp_s(a, 0, b, TOTAL, &diff) == ESZEROL);
    CHECK(diff == -1);

    /* (10) slen > dmax -> ESNOSPC, diff default -1 */
    diff = 99;
    CHECK(cplus_memcmp_s(a, 4, b, TOTAL, &diff) == ESNOSPC);
    CHECK(diff == -1);

    /* (11) NULL dest / NULL src -> ESNULLP, diff default -1 */
    diff = 99;
    CHECK(cplus_memcmp_s(NULL, TOTAL, b, 4, &diff) == ESNULLP);
    CHECK(diff == -1);
    diff = 99;
    CHECK(cplus_memcmp_s(a, TOTAL, NULL, 4, &diff) == ESNULLP);
    CHECK(diff == -1);

    /* (12) dmax > RSIZE_MAX_MEM -> ESLEMAX */
    diff = 99;
    CHECK(cplus_memcmp_s(a, (rsize_t)RSIZE_MAX_MEM + 1, b, 4, &diff) == ESLEMAX);
    CHECK(diff == -1);

    /* (13) slen > RSIZE_MAX_MEM -> ESLEMAX (dmax valid) */
    diff = 99;
    CHECK(cplus_memcmp_s(a, TOTAL, b, (rsize_t)RSIZE_MAX_MEM + 1, &diff) == ESLEMAX);
    CHECK(diff == -1);

    /* (14) memcmp_s does not modify either buffer */
    memset(a, 0x33, sizeof a);
    memset(b, 0x33, sizeof b);
    a[0] = 0x10;
    b[0] = 0x20;
    diff = 0;
    CHECK(cplus_memcmp_s(a, TOTAL, b, TOTAL, &diff) == 0);
    CHECK(diff == -1);
    CHECK(a[1] == 0x33 && b[1] == 0x33);

    if (failures == 0) {
        printf("test_mem50_memcmp_signs: ok\n");
        return 0;
    }
    printf("test_mem50_memcmp_signs: FAILURES=%d\n", failures);
    return 1;
}
