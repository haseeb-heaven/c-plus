/*
 * Bug CLASS: zero-length / bound off-by-one misuse of the memory primitives.
 *
 * Raw C: memcpy(NULL, NULL, 0) is UB by the letter of the standard; memset with
 * a zero count on valid memory is fine. The checked C+ memory adapters give each
 * case a DEFINED outcome:
 *   - cplus_memcpy_s / cplus_memmove_s with smax == 0 do nothing and succeed,
 *     even with NULL pointers (no dereference).
 *   - cplus_memset_s validates dest first, so memset_s(NULL, ...) is rejected
 *     even when n == 0.
 *   - a truthful dmax == 0 for a non-zero length is rejected (ESZEROL).
 *   - an oversized dmax (> RSIZE_MAX_MEM) or oversized smax is rejected.
 *   - an smax that exceeds the (truthful) dmax is rejected with ESNOSPC.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNULLP, ESZEROL, ESLEMAX, ESNOSPC */

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

    char d[8];

    /* --- zero-length ops with NULL are DEFINED (no dereference) --- */
    CHECK(cplus_memcpy_s(NULL, 0, NULL, 0) == 0);
    CHECK(cplus_memcpy_s(NULL, sizeof d, NULL, 0) == 0);
    CHECK(cplus_memmove_s(NULL, 0, NULL, 0) == 0);
    CHECK(cplus_memmove_s(NULL, sizeof d, NULL, 0) == 0);

    /* --- zero-length copy with real, in-bounds pointers also succeeds --- */
    CHECK(cplus_memcpy_s(d, sizeof d, "x", 0) == 0);
    CHECK(cplus_memmove_s(d, sizeof d, d, 0) == 0);

    /* --- non-zero length with NULL is a caught violation --- */
    CHECK(cplus_memcpy_s(NULL, sizeof d, "x", 1) == ESNULLP);
    CHECK(cplus_memmove_s(NULL, sizeof d, "x", 1) == ESNULLP);
    CHECK(cplus_memcpy_s(d, sizeof d, NULL, 1) == ESNULLP);
    CHECK(cplus_memmove_s(d, sizeof d, NULL, 1) == ESNULLP);

    /* --- memset_s(NULL, ...) rejected, even when n == 0 (dest checked first) - */
    CHECK(cplus_memset_s(NULL, sizeof d, 0, 1) == ESNULLP);
    CHECK(cplus_memset_s(NULL, sizeof d, 0, 0) == ESNULLP);
    /* ...but a valid dest with n == 0 is a defined no-op */
    CHECK(cplus_memset_s(d, sizeof d, 0xAA, 0) == 0);

    /* --- dmax == 0 with a non-zero length is rejected --- */
    CHECK(cplus_memcpy_s(d, 0, "x", 1) == ESZEROL);
    CHECK(cplus_memmove_s(d, 0, "x", 1) == ESZEROL);
    CHECK(cplus_memzero_s(d, 0) == ESZEROL);

    /* --- oversized dmax (> RSIZE_MAX_MEM) is rejected --- */
    CHECK(cplus_memcpy_s(d, RSIZE_MAX_MEM + 1, "x", 1) == ESLEMAX);
    CHECK(cplus_memmove_s(d, RSIZE_MAX_MEM + 1, "x", 1) == ESLEMAX);
    CHECK(cplus_memset_s(d, RSIZE_MAX_MEM + 1, 0, 1) == ESLEMAX);
    CHECK(cplus_memzero_s(d, RSIZE_MAX_MEM + 1) == ESLEMAX);

    /* --- oversized smax (> RSIZE_MAX_MEM) is rejected --- */
    CHECK(cplus_memcpy_s(d, sizeof d, "x", RSIZE_MAX_MEM + 1) == ESLEMAX);
    CHECK(cplus_memmove_s(d, sizeof d, "x", RSIZE_MAX_MEM + 1) == ESLEMAX);

    /* --- smax exceeding the truthful dmax is rejected (bound enforced) --- */
    CHECK(cplus_memcpy_s(d, 4, "abcdefgh", 8) == ESNOSPC);
    CHECK(cplus_memmove_s(d, 4, "abcdefgh", 8) == ESNOSPC);

    /* --- a valid bound still works exactly --- */
    memset(d, 0x33, sizeof d);
    CHECK(cplus_memzero_s(d, sizeof d) == 0);
    CHECK(d[0] == 0 && d[sizeof d - 1] == 0);
    CHECK(cplus_memset_s(d, sizeof d, 0x5A, sizeof d) == 0);
    CHECK(d[0] == 0x5A && d[sizeof d - 1] == 0x5A);

    if (failures == 0) {
        printf("bugclass2_zero_and_bounds: ok\n");
        return 0;
    }
    printf("bugclass2_zero_and_bounds: FAILURES=%d\n", failures);
    return 1;
}
