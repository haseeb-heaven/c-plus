/*
 * test_memfam_size_overflow.c
 *
 * Size-overflow handling, mapped to the adversarial catalogue.
 *   bug41  size arithmetic overflow -> oversized length passed unchecked
 *   bug42  length exceeds the supported maximum -> pruning/overflow by truncation
 *
 * Contract: any rsize_t length greater than RSIZE_MAX_MEM is a constraint
 * violation and reports ESLEMAX (403).  The check happens before any copy, so
 * an oversized dmax or smax must never be used as a loop bound and the
 * destination must never be written beyond the declared dmax.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

#define ESLEMAX_C 403

static int all_eq(const void *p, size_t from, size_t to, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = from; i < to; ++i)
        if (b[i] != v) return 0;
    return 1;
}

int main(void) {
    char d[16];
    char s[16];
    int diff = 0;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    memset(s, 0x41, sizeof s);

    /* ---- memcpy_s: oversized dmax is rejected before any write ---- */
    memset(d, 0x5A, sizeof d);
    assert(cplus_memcpy_s(d, RSIZE_MAX_MEM + 1, s, 4) == ESLEMAX_C);
    assert(all_eq(d, 0, sizeof d, 0x5A));

    /* ---- memcpy_s: oversized smax is rejected (only [0,dmax) may be cleared) ---- */
    memset(d, 0x5A, sizeof d);
    assert(cplus_memcpy_s(d, 8, s, RSIZE_MAX_MEM + 1) == ESLEMAX_C);
    assert(all_eq(d, 8, sizeof d, 0x5A));

    /* ---- both sizes oversized at once ---- */
    memset(d, 0x5A, sizeof d);
    assert(cplus_memcpy_s(d, RSIZE_MAX_MEM + 1, s, RSIZE_MAX_MEM + 1) == ESLEMAX_C);
    assert(all_eq(d, 0, sizeof d, 0x5A));

    /* ---- memmove_s mirrors the same overflow checks ---- */
    memset(d, 0x5A, sizeof d);
    assert(cplus_memmove_s(d, RSIZE_MAX_MEM + 1, s, 4) == ESLEMAX_C);
    assert(all_eq(d, 0, sizeof d, 0x5A));

    memset(d, 0x5A, sizeof d);
    assert(cplus_memmove_s(d, 8, s, RSIZE_MAX_MEM + 1) == ESLEMAX_C);
    assert(all_eq(d, 8, sizeof d, 0x5A));

    /* ---- memset_s: oversized dmax / n ---- */
    memset(d, 0x5A, sizeof d);
    assert(cplus_memset_s(d, RSIZE_MAX_MEM + 1, 'A', 4) == ESLEMAX_C);
    assert(all_eq(d, 0, sizeof d, 0x5A));

    memset(d, 0x5A, sizeof d);
    assert(cplus_memset_s(d, 8, 'A', RSIZE_MAX_MEM + 1) == ESLEMAX_C);
    assert(all_eq(d, 8, sizeof d, 0x5A)); /* clamped to dmax on the way out */

    /* ---- memzero_s: oversized length ---- */
    memset(d, 0x5A, sizeof d);
    assert(cplus_memzero_s(d, RSIZE_MAX_MEM + 1) == ESLEMAX_C);
    assert(all_eq(d, 0, sizeof d, 0x5A));

    /* ---- memcmp_s: oversized dmax / slen, diff reset to -1 on violation ---- */
    diff = 77;
    assert(cplus_memcmp_s(s, RSIZE_MAX_MEM + 1, s, 4, &diff) == ESLEMAX_C);
    assert(diff == -1);
    diff = 77;
    assert(cplus_memcmp_s(s, 4, s, RSIZE_MAX_MEM + 1, &diff) == ESLEMAX_C);
    assert(diff == -1);

    /* ---- a normal call still works as a control ---- */
    assert(cplus_memcpy_s(d, sizeof d, s, sizeof s) == 0);
    assert(memcmp(d, s, sizeof s) == 0);

    puts("test_memfam_size_overflow: ok");
    return 0;
}
