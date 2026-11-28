/*
 * test_memfam_error_codes.c
 *
 * Pins the exact constraint codes returned by the checked memory family, so a
 * regression that swaps or reorders the checks is caught.  These are the codes
 * the adversarial catalogue relies on to distinguish failure classes:
 *
 *   ESNULLP 400  null pointer
 *   ESZEROL 401  zero length
 *   ESLEMAX 403  size > RSIZE_MAX_MEM
 *   ESOVRLP 404  undefined overlap
 *   ESNOSPC 406  destination too small (dmax < requested)
 *
 * The codes are positive; success is exactly 0.  Callers must test != 0.
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
#define ESOVRLP_C 404
#define ESNOSPC_C 406

static int all_eq(const void *p, size_t from, size_t to, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = from; i < to; ++i)
        if (b[i] != v) return 0;
    return 1;
}

int main(void) {
    char d[16];
    char ov[16];
    int diff;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- the codes are the documented positive values ---- */
    assert(ESNULLP_C == 400);
    assert(ESZEROL_C == 401);
    assert(ESLEMAX_C == 403);
    assert(ESOVRLP_C == 404);
    assert(ESNOSPC_C == 406);

    /* ---- ESNULLP (400) ---- */
    assert(cplus_memcpy_s(NULL, 4, "abcd", 4) == ESNULLP_C);
    assert(cplus_memcpy_s(d, 4, NULL, 4) == ESNULLP_C);
    assert(cplus_memmove_s(NULL, 4, "abcd", 4) == ESNULLP_C);
    assert(cplus_memset_s(NULL, 4, 'A', 4) == ESNULLP_C);
    assert(cplus_memzero_s(NULL, 4) == ESNULLP_C);
    assert(cplus_memcmp_s(NULL, 4, "abcd", 4, &diff) == ESNULLP_C);

    /* ---- ESZEROL (401) ---- */
    assert(cplus_memcpy_s(d, 0, "abcd", 4) == ESZEROL_C);
    assert(cplus_memmove_s(d, 0, "abcd", 4) == ESZEROL_C);
    assert(cplus_memzero_s(d, 0) == ESZEROL_C);
    assert(cplus_memcmp_s("abc", 0, "abc", 3, &diff) == ESZEROL_C);

    /* ---- ESLEMAX (403): size beyond RSIZE_MAX_MEM ---- */
    assert(cplus_memcpy_s(d, RSIZE_MAX_MEM + 1, "abcd", 4) == ESLEMAX_C);
    assert(cplus_memcpy_s(d, 4, "abcd", RSIZE_MAX_MEM + 1) == ESLEMAX_C);
    assert(cplus_memmove_s(d, RSIZE_MAX_MEM + 1, "abcd", 4) == ESLEMAX_C);
    assert(cplus_memzero_s(d, RSIZE_MAX_MEM + 1) == ESLEMAX_C);
    assert(cplus_memset_s(d, RSIZE_MAX_MEM + 1, 'A', 4) == ESLEMAX_C);

    /* ---- ESNOSPC (406): destination too small ---- */
    assert(cplus_memcpy_s(d, 4, "abcdefgh", 8) == ESNOSPC_C);
    assert(cplus_memmove_s(d, 4, "abcdefgh", 8) == ESNOSPC_C);
    assert(cplus_memset_s(d, 4, 'A', 8) == ESNOSPC_C);
    assert(cplus_memcmp_s("ab", 2, "abc", 3, &diff) == ESNOSPC_C);

    /* ---- ESOVRLP (404): undefined overlap in memcpy_s ---- */
    memset(ov, 0x5A, sizeof ov);
    assert(cplus_memcpy_s(ov + 1, 8, ov, 8) == ESOVRLP_C);
    assert(all_eq(ov, 9, sizeof ov, 0x5A));

    /* ---- success is exactly 0 (and the codes are not negated) ---- */
    assert(cplus_memcpy_s(d, sizeof d, "hi", 3) == 0);
    assert(cplus_memcpy_s(NULL, 0, NULL, 0) == 0);
    assert(ESNULLP_C > 0 && ESZEROL_C > 0 && ESOVRLP_C > 0 && ESNOSPC_C > 0);

    puts("test_memfam_error_codes: ok");
    return 0;
}
