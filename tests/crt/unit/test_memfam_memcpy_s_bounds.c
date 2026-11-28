/*
 * test_memfam_memcpy_s_bounds.c
 *
 * Checked memcpy_s: size/bounds behaviour mapped to the adversarial catalogue.
 *   bug23  heap buffer overflow   -> smax larger than dmax must be rejected
 *   bug26  off-by-one overflow     -> dmax one short of the object must not write
 *   bug36  missing sizeof          -> a hard-coded small dmax must not overflow
 *
 * The observable invariant: a call never writes past the declared dmax.  Any
 * byte at index >= dmax ("guard byte") must survive both success and rejection,
 * regardless of the real size of the object.  Success returns 0; a size
 * violation returns ESNOSPC (406) when smax > dmax, ESLEMAX (403) when a size
 * exceeds RSIZE_MAX_MEM.
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
    char dst[16];
    char src[16];

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    memset(src, 0x41, sizeof src);

    /* ---- 1) exact fit (smax == dmax == object size) ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memcpy_s(dst, 8, "12345678", 8) == 0);
    assert(memcmp(dst, "12345678", 8) == 0);

    /* ---- 2) guard bytes past dmax survive a successful copy ---- */
    assert(all_eq(dst, 8, sizeof dst, 0xCC));

    /* ---- 3) bug26 off-by-one: dmax one short of smax is rejected ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memcpy_s(dst, 7, "12345678", 8) == ESNOSPC_C);
    assert(all_eq(dst, 7, sizeof dst, 0xCC)); /* never wrote past dmax=7 */

    /* ---- 4) bug23 heap overflow pattern: big smax into small dmax ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memcpy_s(dst, 4, "0123456789abcdef", 16) == ESNOSPC_C);
    assert(all_eq(dst, 4, sizeof dst, 0xCC));

    /* ---- 5) bug36 missing sizeof: hard-coded literal dmax, correct smax ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memcpy_s(dst, 4, "abcdefgh", 8) == ESNOSPC_C); /* smax 8 > dmax 4 */
    assert(all_eq(dst, 4, sizeof dst, 0xCC));

    /* ---- 6) same call with the intended (declared) dmax succeeds ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memcpy_s(dst, 4, "abcd", 4) == 0);
    assert(memcmp(dst, "abcd", 4) == 0);
    assert(all_eq(dst, 4, sizeof dst, 0xCC));

    /* ---- 7) smax just one over dmax, mid-range sizes ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memcpy_s(dst, 12, src, 13) == ESNOSPC_C);
    assert(all_eq(dst, 12, sizeof dst, 0xCC));

    /* ---- 8) copy into the middle of a large buffer stays bounded ---- */
    memset(src, 0x41, sizeof src);
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memcpy_s(dst, sizeof dst, src, sizeof src) == 0);
    assert(memcmp(dst, src, sizeof src) == 0);

    /* ---- 9) zero dmax with a non-zero request is a length violation ---- */
    assert(cplus_memcpy_s(dst, 0, src, 4) == ESZEROL_C);

    /* ---- 10) a copy that writes fewer bytes than dmax leaves the tail ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memcpy_s(dst, sizeof dst, "xy", 3) == 0);
    assert(memcmp(dst, "xy", 3) == 0);
    assert(all_eq(dst, 3, sizeof dst, 0xCC));

    puts("test_memfam_memcpy_s_bounds: ok");
    return 0;
}
