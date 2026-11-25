/*
 * test_memfam_null_and_zerolen.c
 *
 * Null-pointer and zero-length behaviour of the checked memory family.
 *
 *   bug04  null pointer passed to memcpy (and friends) must be rejected with a
 *          constraint code, never dereferenced.
 *   bug191 zero-length operation with NULL operands must be a defined no-op
 *          (success) rather than a crash, while a genuinely invalid combination
 *          (non-zero count with NULL, or a NULL destination for memset_s) is
 *          still caught.
 *
 * Codes: ESNULLP (400) null pointer, ESZEROL (401) zero length.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

#define ESNULLP_C 400
#define ESZEROL_C 401

static int all_eq(const void *p, size_t from, size_t to, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = from; i < to; ++i)
        if (b[i] != v) return 0;
    return 1;
}

int main(void) {
    char d[8];
    int diff = 0;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- bug04: NULL with a real copy request is rejected (never deref'd) ---- */
    assert(cplus_memcpy_s(NULL, sizeof d, "abc", 4) == ESNULLP_C);
    assert(cplus_memcpy_s(d, sizeof d, NULL, 1) == ESNULLP_C);
    assert(cplus_memcpy_s(d, sizeof d, NULL, 4) == ESNULLP_C);
    assert(cplus_memmove_s(NULL, sizeof d, "abc", 4) == ESNULLP_C);
    assert(cplus_memmove_s(d, sizeof d, NULL, 4) == ESNULLP_C);
    assert(cplus_memset_s(NULL, sizeof d, 'A', 4) == ESNULLP_C);
    assert(cplus_memzero_s(NULL, sizeof d) == ESNULLP_C);
    assert(cplus_memcmp_s(NULL, 4, "abcd", 4, &diff) == ESNULLP_C);
    assert(cplus_memcmp_s("abcd", 4, NULL, 4, &diff) == ESNULLP_C);
    assert(cplus_memcmp_s("abcd", 4, "abcd", 4, NULL) == ESNULLP_C);

    /* ---- bug191: zero-length with NULL operands is a defined no-op ---- */
    memset(d, 0x5A, sizeof d);
    assert(cplus_memcpy_s(NULL, 0, NULL, 0) == 0);
    assert(cplus_memcpy_s(NULL, sizeof d, NULL, 0) == 0);
    assert(cplus_memcpy_s(d, 0, NULL, 0) == 0);
    assert(cplus_memcpy_s(d, sizeof d, NULL, 0) == 0);
    assert(cplus_memmove_s(NULL, 0, NULL, 0) == 0);
    assert(cplus_memmove_s(NULL, sizeof d, "abc", 0) == 0);
    assert(cplus_memmove_s(d, sizeof d, NULL, 0) == 0);
    assert(cplus_memset_s(d, sizeof d, 'Z', 0) == 0); /* n == 0 no-op */

    /* memset_s is asymmetric: a NULL destination is always a violation */
    assert(cplus_memset_s(NULL, 0, 0, 0) == ESNULLP_C);
    assert(cplus_memset_s(NULL, sizeof d, 0, 0) == ESNULLP_C);

    /* the zero-length no-ops above left the destination untouched */
    assert(all_eq(d, 0, sizeof d, 0x5A));

    /* ---- a zero dmax (or zero length) with a real request is caught ---- */
    assert(cplus_memcpy_s(d, 0, "abc", 4) == ESZEROL_C);
    assert(cplus_memmove_s(d, 0, "abc", 4) == ESZEROL_C);
    assert(cplus_memzero_s(d, 0) == ESZEROL_C);
    assert(cplus_memcmp_s(d, 0, d, 4, &diff) == ESZEROL_C);
    assert(cplus_memcmp_s(d, 4, d, 0, &diff) == ESZEROL_C);
    assert(all_eq(d, 0, sizeof d, 0x5A)); /* still untouched */

    /* ---- valid operations still behave, for contrast ---- */
    assert(cplus_memcpy_s(d, sizeof d, "hi", 3) == 0);
    assert(memcmp(d, "hi", 3) == 0);
    assert(cplus_memzero_s(d, sizeof d) == 0);
    assert(all_eq(d, 0, sizeof d, 0x00));

    puts("test_memfam_null_and_zerolen: ok");
    return 0;
}
