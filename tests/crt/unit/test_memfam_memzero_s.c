/*
 * test_memfam_memzero_s.c
 *
 * Checked memzero_s: zero a region of exactly dmax bytes.
 *
 * Unlike the C11 memory functions, memzero_s treats a zero length as a
 * constraint violation (ESZEROL 401) and a NULL destination as ESNULLP (400);
 * a length above RSIZE_MAX_MEM is ESLEMAX (403).  It must never write past the
 * declared length, so guard bytes survive.
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

static int all_eq(const void *p, size_t from, size_t to, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = from; i < to; ++i)
        if (b[i] != v) return 0;
    return 1;
}

int main(void) {
    char b[16];

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- 1) zero a full buffer ---- */
    memset(b, 0xAB, sizeof b);
    assert(cplus_memzero_s(b, sizeof b) == 0);
    assert(all_eq(b, 0, sizeof b, 0x00));

    /* ---- 2) zero a prefix; the tail is preserved ---- */
    memset(b, 0xCD, sizeof b);
    assert(cplus_memzero_s(b, 5) == 0);
    assert(all_eq(b, 0, 5, 0x00));
    assert(all_eq(b, 5, sizeof b, 0xCD));

    /* ---- 3) zeroing is idempotent on already-zero memory ---- */
    memset(b, 0x00, sizeof b);
    assert(cplus_memzero_s(b, sizeof b) == 0);
    assert(all_eq(b, 0, sizeof b, 0x00));

    /* ---- 4) exactly one byte ---- */
    memset(b, 0xEE, sizeof b);
    assert(cplus_memzero_s(b, 1) == 0);
    assert(b[0] == 0);
    assert(all_eq(b, 1, sizeof b, 0xEE));

    /* ---- 5) bug23/26: the tail past the requested length is never touched ---- */
    memset(b, 0x77, sizeof b);
    assert(cplus_memzero_s(b, 8) == 0);
    assert(all_eq(b, 0, 8, 0x00));
    assert(all_eq(b, 8, sizeof b, 0x77));

    /* ---- 6) NULL destination is rejected ---- */
    assert(cplus_memzero_s(NULL, sizeof b) == ESNULLP_C);
    assert(cplus_memzero_s(NULL, 1) == ESNULLP_C);

    /* ---- 7) a zero length is a constraint violation ---- */
    memset(b, 0x99, sizeof b);
    assert(cplus_memzero_s(b, 0) == ESZEROL_C);
    assert(all_eq(b, 0, sizeof b, 0x99)); /* nothing written */

    /* ---- 8) oversized length is rejected before any write ---- */
    assert(cplus_memzero_s(b, RSIZE_MAX_MEM + 1) == ESLEMAX_C);
    assert(all_eq(b, 0, sizeof b, 0x99));

    /* ---- 9) covers values that a memset_s with the same bounds would set ---- */
    memset(b, 'Z', sizeof b);
    assert(cplus_memzero_s(b, 6) == 0);
    assert(cplus_memset_s(b, sizeof b, 'Z', 6) == 0); /* contrasting call */
    assert(all_eq(b, 0, 6, 'Z'));

    puts("test_memfam_memzero_s: ok");
    return 0;
}
