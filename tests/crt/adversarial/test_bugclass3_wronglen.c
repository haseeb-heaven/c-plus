/*
 * Bug CLASS 3c: mismatched-length copy -- sizing the transfer from the SOURCE
 * object while the DESTINATION is smaller.
 *
 * Raw C (oob_wrongarray.c) bounds a loop by one array's length but indexes a
 * shorter one, so it walks past the end. The same confusion in a copy is the
 * idiom `cplus_memcpy_s(dst, sizeof dst, src, sizeof src)` when `sizeof src >
 * sizeof dst`: the checked C+ adapter rejects it with ESNOSPC (the source
 * length exceeds the truthful destination bound) instead of overrunning the
 * destination. Correctly matched sizes succeed.
 *
 *   - sizeof src > sizeof dst  -> rejected (ESNOSPC), dst untouched
 *   - sizeof src == sizeof dst -> succeeds
 *   - sizeof src < sizeof dst  -> succeeds (bounded by the smaller length)
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNOSPC */

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

    /* --- the exact idiom, src larger than dst: rejected --- */
    {
        unsigned char dst[3];
        const unsigned char src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        errno_t r;
        memset(dst, 0xC3, sizeof dst);
        r = cplus_memcpy_s(dst, sizeof dst, src, sizeof src); /* 3 vs 8 */
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        /* rejected copy leaves the destination intact */
        CHECK(dst[0] == 0xC3 && dst[1] == 0xC3 && dst[2] == 0xC3);
    }

    /* --- guard byte just past the small destination is untouched --- */
    {
        unsigned char arena[8];
        const unsigned char src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        memset(arena, 0x7A, sizeof arena);
        /* dst is arena[0..2] (3 bytes); truthful dmax = 3, smax = 8 */
        CHECK(cplus_memcpy_s(arena, 3, src, sizeof src) == ESNOSPC);
        CHECK(arena[3] == 0x7A);
        CHECK(arena[7] == 0x7A);
    }

    /* --- correct sizes (equal) succeed --- */
    {
        unsigned char dst[8];
        const unsigned char src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        memset(dst, 0x00, sizeof dst);
        CHECK(cplus_memcpy_s(dst, sizeof dst, src, sizeof src) == 0);
        CHECK(memcmp(dst, src, 8) == 0);
    }

    /* --- destination larger than source: bounded by the smaller length --- */
    {
        unsigned char dst[8];
        const unsigned char src[3] = { 9, 8, 7 };
        memset(dst, 0x00, sizeof dst);
        CHECK(cplus_memcpy_s(dst, sizeof dst, src, sizeof src) == 0);
        CHECK(dst[0] == 9 && dst[1] == 8 && dst[2] == 7);
        CHECK(dst[3] == 0x00);
    }

    /* --- typed arrays: 12-byte int[3] destination, 20-byte int[5] source --- */
    {
        int dst[3];
        int src[5] = { 1, 2, 3, 4, 5 };
        errno_t r;
        memset(dst, 0x7E, sizeof dst);
        r = cplus_memcpy_s(dst, sizeof dst, src, sizeof src); /* 12 vs 20 */
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        CHECK(((unsigned char *)dst)[0] == 0x7E); /* intact, not corrupted */
        CHECK(((unsigned char *)dst)[sizeof dst - 1] == 0x7E);
    }

    /* --- typed arrays: equal sizes succeed, contents land intact --- */
    {
        int dst[5];
        int src[5] = { 1, 2, 3, 4, 5 };
        memset(dst, 0x00, sizeof dst);
        CHECK(cplus_memcpy_s(dst, sizeof dst, src, sizeof src) == 0);
        CHECK(dst[0] == 1);
        CHECK(dst[4] == 5);
    }

    if (failures == 0) {
        printf("bugclass3_wronglen: ok\n");
        return 0;
    }
    printf("bugclass3_wronglen: FAILURES=%d\n", failures);
    return 1;
}
