/*
 * test_memfam_memset_s.c
 *
 * Checked memset_s behaviour and its interaction with dmax.
 *
 *   bug23  heap overflow   -> n must never exceed dmax in the write
 *   bug26  off-by-one      -> writing exactly dmax is fine, dmax+1 is not
 *   bug191 zero-length     -> n == 0 is a defined no-op
 *
 * Codes: ESNOSPC (406) when n > dmax, ESLEMAX (403) when value > 255 or a size
 * exceeds RSIZE_MAX_MEM, ESNULLP (400) when dest is NULL.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

#define ESNULLP_C 400
#define ESLEMAX_C 403
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

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- 1) set fewer bytes than dmax; tail preserved ---- */
    memset(d, 0xCC, sizeof d);
    assert(cplus_memset_s(d, sizeof d, 'A', 4) == 0);
    assert(all_eq(d, 0, 4, 'A'));
    assert(all_eq(d, 4, sizeof d, 0xCC));

    /* ---- 2) exactly dmax bytes; guards past dmax preserved ---- */
    memset(d, 0xCC, sizeof d);
    assert(cplus_memset_s(d, 8, 'B', 8) == 0);
    assert(all_eq(d, 0, 8, 'B'));
    assert(all_eq(d, 8, sizeof d, 0xCC));

    /* ---- 3) bug26 off-by-one: n == dmax + 1 rejected, clamped to dmax ---- */
    memset(d, 0xCC, sizeof d);
    assert(cplus_memset_s(d, 4, 'C', 5) == ESNOSPC_C);
    assert(all_eq(d, 0, 4, 'C'));       /* wrote dmax bytes ... */
    assert(all_eq(d, 4, sizeof d, 0xCC)); /* ... but never past dmax */

    /* ---- 4) bug23 heap overflow: n far beyond dmax ---- */
    memset(d, 0xCC, sizeof d);
    assert(cplus_memset_s(d, 2, 'D', 16) == ESNOSPC_C);
    assert(all_eq(d, 2, sizeof d, 0xCC));

    /* ---- 5) bug191: n == 0 is a defined no-op (dest untouched) ---- */
    memset(d, 0xCC, sizeof d);
    assert(cplus_memset_s(d, sizeof d, 'E', 0) == 0);
    assert(all_eq(d, 0, sizeof d, 0xCC));

    /* ---- 6) value > 255 is a constraint violation; dest untouched ---- */
    memset(d, 0xCC, sizeof d);
    assert(cplus_memset_s(d, sizeof d, 300, 4) == ESLEMAX_C);
    assert(all_eq(d, 0, sizeof d, 0xCC));

    /* ---- 7) value boundary values 0 and 255 are accepted ---- */
    memset(d, 0xCC, sizeof d);
    assert(cplus_memset_s(d, 4, 0, 4) == 0);
    assert(all_eq(d, 0, 4, 0x00));
    assert(cplus_memset_s(d, 4, 255, 4) == 0);
    assert(all_eq(d, 0, 4, 0xFF));

    /* ---- 8) NULL destination is rejected even for n == 0 ---- */
    assert(cplus_memset_s(NULL, sizeof d, 'A', 4) == ESNULLP_C);
    assert(cplus_memset_s(NULL, 0, 0, 0) == ESNULLP_C);

    /* ---- 9) dmax == 0 with a positive n cannot write ---- */
    memset(d, 0xCC, sizeof d);
    assert(cplus_memset_s(d, 0, 'F', 3) == ESNOSPC_C);
    assert(all_eq(d, 0, sizeof d, 0xCC));

    puts("test_memfam_memset_s: ok");
    return 0;
}
