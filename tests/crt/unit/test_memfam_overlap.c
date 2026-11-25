/*
 * test_memfam_overlap.c
 *
 * Overlapping-region behaviour, mapped to the adversarial catalogue.
 *   bug38  memcpy on overlapping regions -> undefined result
 *   bug40  self-overlapping copy / memmove confusion -> wrong bytes
 *
 * Contract: memcpy_s requires non-overlapping regions and rejects overlap with
 * ESOVRLP (404); a copy between the same pointer is explicitly allowed.
 * memmove_s is defined for overlap (it behaves like memmove) and must produce
 * the correct result while still honouring dmax.
 *
 * The observability invariant: no write ever lands at index >= dmax, so guard
 * bytes beyond dmax survive.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

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
    char a[16];
    char b[16];
    char c[16];
    char m[16];
    char n[16];
    char d1[8];
    char d2[8];

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- bug38: forward overlap (dest above src) is rejected ---- */
    memset(a, 0xCC, sizeof a);
    memcpy(a, "abcdefgh", 8);
    assert(cplus_memcpy_s(a + 1, 8, a, 8) == ESOVRLP_C);
    /* on ESOVRLP the first dmax bytes are cleared; guards past dmax survive */
    assert(all_eq(a, 9, sizeof a, 0xCC));

    /* ---- bug38: backward overlap (dest below src) is rejected too ---- */
    memset(b, 0xCC, sizeof b);
    memcpy(b, "abcdefgh", 8);
    assert(cplus_memcpy_s(b, 8, b + 1, 7) == ESOVRLP_C);
    assert(all_eq(b, 8, sizeof b, 0xCC));

    /* ---- overlap even when smax < dmax is still rejected ---- */
    memset(c, 0xCC, sizeof c);
    memcpy(c, "abcdefgh", 8);
    assert(cplus_memcpy_s(c + 2, 12, c, 8) == ESOVRLP_C);
    assert(all_eq(c, 14, sizeof c, 0xCC)); /* dp=c+2, dmax=12 => clears [2,14) */

    /* ---- memcpy_s with identical dest == src is explicitly allowed ---- */
    memset(c, 0xCC, sizeof c);
    memcpy(c, "abcdefgh", 8);
    assert(cplus_memcpy_s(c, 8, c, 8) == 0);
    assert(memcmp(c, "abcdefgh", 8) == 0);
    assert(all_eq(c, 8, sizeof c, 0xCC));

    /* ---- distinct, non-overlapping objects: plain success ---- */
    memcpy(d2, "mondays", 8);
    assert(cplus_memcpy_s(d1, sizeof d1, d2, 8) == 0);
    assert(memcmp(d1, "mondays", 8) == 0);

    /* ---- bug40: memmove_s handles forward overlap correctly ---- */
    memset(m, 0xCC, sizeof m);
    memcpy(m, "abcdefgh", 8);
    assert(cplus_memmove_s(m + 1, 15, m, 8) == 0);
    assert(memcmp(m, "aabcdefgh", 9) == 0);
    assert(all_eq(m, 9, sizeof m, 0xCC)); /* exactly 8 bytes written at [1,9) */

    /* ---- bug40: memmove_s handles backward overlap correctly ---- */
    memset(n, 0xCC, sizeof n);
    memcpy(n, "abcdefgh", 8);
    assert(cplus_memmove_s(n, 15, n + 1, 4) == 0);
    assert(memcmp(n, "bcdeefgh", 8) == 0);
    assert(all_eq(n, 8, sizeof n, 0xCC));

    /* ---- memmove_s with identical dest == src is a no-op success ---- */
    assert(cplus_memmove_s(m, sizeof m, m, 8) == 0);
    assert(memcmp(m, "aabcdefgh", 9) == 0);

    /* ---- memmove_s still enforces the size constraint ---- */
    memset(m, 0xCC, sizeof m);
    assert(cplus_memmove_s(m, 4, m + 8, 8) == ESNOSPC_C); /* smax 8 > dmax 4 */
    assert(all_eq(m, 4, sizeof m, 0xCC));

    puts("test_memfam_overlap: ok");
    return 0;
}
