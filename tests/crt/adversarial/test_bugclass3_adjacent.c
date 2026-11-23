/*
 * Bug CLASS 3a: "adjacent" one-past-the-bound access.
 *
 * Raw C (oob_adjacent.c): a loop bounded by len but a subscript that adds an
 * offset reads arr[len] -- one element past the end. The checked C+ memory
 * adapters make the SAME shape a defined constraint error: a caller that
 * passes a TRUTHFUL dmax (the real size of the bounded region) and then asks
 * for one byte MORE (smax == dmax + 1) is rejected with ESNOSPC, and the guard
 * byte immediately past dmax is left intact.
 *
 *   - cplus_memcpy_s(dst, dmax, src, dmax + 1)  -> rejected (write path)
 *   - cplus_memcmp_s(dst, dmax, src, dmax + 1)  -> rejected (read path)
 *   - the exact same call with smax == dmax      -> succeeds
 *   - a byte placed just past the bounded region is never touched
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNOSPC, ESZEROL, ESLEMAX */

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
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* --- WRITE one past a bounded region: rejected, guard byte intact --- */
    {
        unsigned char dst[8];
        const unsigned char src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        errno_t r;
        memset(dst, 0xEE, sizeof dst);       /* 0xEE == "untouched" sentinel */
        /* truthful dmax = 4, but the caller asks for 5 bytes: one too many */
        r = cplus_memcpy_s(dst, 4, src, 5);
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        CHECK(dst[4] == 0xEE);               /* guard byte past dmax intact */
        CHECK(dst[5] == 0xEE && dst[6] == 0xEE && dst[7] == 0xEE);
        /* C+ security contract: a rejected copy leaves dst intact (no OOB-prone dmax wipe). */
        CHECK(dst[0] == 0xEE && dst[1] == 0xEE && dst[2] == 0xEE && dst[3] == 0xEE);
    }

    /* --- same shape, grossly over-long request: still rejected --- */
    {
        unsigned char dst[8];
        memset(dst, 0x5C, sizeof dst);
        CHECK(cplus_memcpy_s(dst, 4, "ABCDEFGH", 8) == ESNOSPC);
        CHECK(dst[4] == 0x5C);
        CHECK(dst[7] == 0x5C);
    }

    /* --- READ one past a bounded region via memcmp_s: rejected --- */
    {
        const unsigned char a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const unsigned char b[8] = { 1, 2, 3, 4, 9, 9, 9, 9 };
        const unsigned char c[4] = { 1, 2, 3, 2 }; /* a[3]=4 > c[3]=2 */
        int diff = 12345;
        /* truthful dmax = 4, but the caller compares 5 bytes: one too many */
        CHECK(cplus_memcmp_s(a, 4, b, 5, &diff) != 0);
        CHECK(cplus_memcmp_s(a, 4, b, 5, &diff) == ESNOSPC);
        /* the in-bounds prefix compare succeeds and reports equality */
        CHECK(cplus_memcmp_s(a, 4, b, 4, &diff) == 0);
        CHECK(diff == 0);
        /* a genuine in-bounds difference is reported through diff */
        CHECK(cplus_memcmp_s(a, 4, c, 4, &diff) == 0);
        CHECK(diff > 0);
    }

    /* --- the correct, exactly-in-bounds write still succeeds --- */
    {
        unsigned char dst[8];
        memset(dst, 0x00, sizeof dst);
        CHECK(cplus_memcpy_s(dst, 4, "WXYZ", 4) == 0);
        CHECK(memcmp(dst, "WXYZ", 4) == 0);
        CHECK(dst[4] == 0x00);               /* guard byte past dmax intact */
    }

    if (failures == 0) {
        printf("bugclass3_adjacent: ok\n");
        return 0;
    }
    printf("bugclass3_adjacent: FAILURES=%d\n", failures);
    return 1;
}
