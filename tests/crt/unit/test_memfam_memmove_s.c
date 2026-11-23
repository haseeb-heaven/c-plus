/*
 * test_memfam_memmove_s.c
 *
 * Checked memmove_s: ordinary (non-overlapping) moves, bounds enforcement and
 * the guard-byte invariant.  Complements test_memfam_overlap.c which focuses on
 * overlapping moves (bug38/bug40); here we pin down the plain contract.
 *
 *   bug23/36 heap overflow / missing sizeof -> smax > dmax must be rejected
 *   bug26    off-by-one                     -> dmax one short must not write
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

#define ESNULLP_C 400
#define ESZEROL_C 401
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

    memset(src, 0xC3, sizeof src);

    /* ---- 1) basic move, exact fit ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memmove_s(dst, 8, "12345678", 8) == 0);
    assert(memcmp(dst, "12345678", 8) == 0);
    assert(all_eq(dst, 8, sizeof dst, 0xCC));

    /* ---- 2) smax < dmax: only smax bytes written, tail untouched ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memmove_s(dst, sizeof dst, "xyz", 4) == 0);
    assert(memcmp(dst, "xyz", 4) == 0);
    assert(all_eq(dst, 4, sizeof dst, 0xCC));

    /* ---- 3) full-buffer move ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memmove_s(dst, sizeof dst, src, sizeof src) == 0);
    assert(memcmp(dst, src, sizeof src) == 0);

    /* ---- 4) bug26 off-by-one: dmax one short of smax ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memmove_s(dst, 7, "12345678", 8) == ESNOSPC_C);
    assert(all_eq(dst, 7, sizeof dst, 0xCC));

    /* ---- 5) bug23/36: smax much larger than dmax ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memmove_s(dst, 4, "0123456789abcdef", 16) == ESNOSPC_C);
    assert(all_eq(dst, 4, sizeof dst, 0xCC));

    /* ---- 6) smax == dmax + 1, mid buffer ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memmove_s(dst, 11, src, 12) == ESNOSPC_C);
    assert(all_eq(dst, 11, sizeof dst, 0xCC));

    /* ---- 7) NULL / zero-length constraint reporting ---- */
    assert(cplus_memmove_s(NULL, sizeof dst, src, 4) == ESNULLP_C);
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memmove_s(dst, sizeof dst, NULL, 4) == ESNULLP_C);
    /* C+ SECURITY PATCH: no dmax-byte wipe on refusal (it could overrun an
     * undersized destination); the return code is the refusal contract. */
    assert(all_eq(dst, 0xCC, sizeof dst, 0xCC));
    assert(cplus_memmove_s(dst, 0, src, 4) == ESZEROL_C);

    /* ---- 8) zero-length move is a no-op success, even with NULL ---- */
    memset(dst, 0xCC, sizeof dst);
    assert(cplus_memmove_s(NULL, 0, NULL, 0) == 0);
    assert(cplus_memmove_s(dst, sizeof dst, NULL, 0) == 0);
    assert(all_eq(dst, 0, sizeof dst, 0xCC));

    /* ---- 9) moving between two independent buffers ---- */
    memset(dst, 0x00, sizeof dst);
    assert(cplus_memmove_s(dst, sizeof dst, "memmove", 8) == 0);
    assert(memcmp(dst, "memmove", 8) == 0);

    puts("test_memfam_memmove_s: ok");
    return 0;
}
