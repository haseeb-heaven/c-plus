/*
 * test_memfam_memcmp_s.c
 *
 * Checked memcmp_s: ordering result and constraint reporting.
 *
 * The comparator reports the ordering through the out-parameter *diff
 * (-1 / 0 / +1) and returns an error code only for constraint violations.
 * On any violation *diff is reset to -1.
 *
 * Codes: ESNULLP (400), ESZEROL (401), ESLEMAX (403), ESNOSPC (406).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

#define ESNULLP_C 400
#define ESZEROL_C 401
#define ESLEMAX_C 403
#define ESNOSPC_C 406

int main(void) {
    int diff;
    char same[4];
    char n1[4];
    char n2[4];

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- 1) equal regions => diff 0 ---- */
    diff = 99;
    assert(cplus_memcmp_s("abc", 3, "abc", 3, &diff) == 0);
    assert(diff == 0);

    /* ---- 2) first less => diff -1 ---- */
    diff = 99;
    assert(cplus_memcmp_s("abc", 3, "abd", 3, &diff) == 0);
    assert(diff == -1);

    /* ---- 3) first greater => diff +1 ---- */
    diff = 99;
    assert(cplus_memcmp_s("abe", 3, "abd", 3, &diff) == 0);
    assert(diff == 1);

    /* ---- 4) identical pointer is equal without comparing ---- */
    same[0] = 1; same[1] = 2; same[2] = 3; same[3] = 4;
    diff = 99;
    assert(cplus_memcmp_s(same, 4, same, 4, &diff) == 0);
    assert(diff == 0);

    /* ---- 5) embedded NUL bytes are compared as raw memory ---- */
    n1[0] = 'a'; n1[1] = '\0'; n1[2] = 'b'; n1[3] = 'c';
    n2[0] = 'a'; n2[1] = '\0'; n2[2] = 'b'; n2[3] = 'd';
    diff = 0;
    assert(cplus_memcmp_s(n1, 4, n2, 4, &diff) == 0);
    assert(diff == -1);

    /* ---- 6) unsigned byte ordering (0x00 < 0xFF), not signed char ---- */
    diff = 0;
    assert(cplus_memcmp_s("\x00", 1, "\xFF", 1, &diff) == 0);
    assert(diff == -1);

    /* ---- 7) NULL diff pointer is rejected ---- */
    assert(cplus_memcmp_s("abc", 3, "abc", 3, NULL) == ESNULLP_C);

    /* ---- 8) NULL dest / src rejected, diff reset to -1 ---- */
    diff = 77;
    assert(cplus_memcmp_s(NULL, 3, "abc", 3, &diff) == ESNULLP_C);
    assert(diff == -1);
    diff = 77;
    assert(cplus_memcmp_s("abc", 3, NULL, 3, &diff) == ESNULLP_C);
    assert(diff == -1);

    /* ---- 9) zero dmax / zero slen are length violations (ESZEROL) ---- */
    diff = 77;
    assert(cplus_memcmp_s("abc", 0, "abc", 3, &diff) == ESZEROL_C);
    assert(diff == -1);
    diff = 77;
    assert(cplus_memcmp_s("abc", 3, "abc", 0, &diff) == ESZEROL_C);
    assert(diff == -1);

    /* ---- 10) slen > dmax => ESNOSPC ---- */
    diff = 77;
    assert(cplus_memcmp_s("ab", 2, "abc", 3, &diff) == ESNOSPC_C);
    assert(diff == -1);

    /* ---- 11) oversized dmax / slen => ESLEMAX ---- */
    diff = 77;
    assert(cplus_memcmp_s("abc", RSIZE_MAX_MEM + 1, "abc", 3, &diff) == ESLEMAX_C);
    assert(diff == -1);
    diff = 77;
    assert(cplus_memcmp_s("abc", 3, "abc", RSIZE_MAX_MEM + 1, &diff) == ESLEMAX_C);
    assert(diff == -1);

    /* ---- 12) slen < dmax only compares the requested prefix ---- */
    diff = 99;
    assert(cplus_memcmp_s("abZ", 3, "abC", 2, &diff) == 0);
    assert(diff == 0);

    puts("test_memfam_memcmp_s: ok");
    return 0;
}
