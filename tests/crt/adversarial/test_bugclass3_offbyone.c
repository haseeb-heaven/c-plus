/*
 * Bug CLASS 3b: classic off-by-one (`<= len` instead of `< len`).
 *
 * Raw C (oob_offbyone.c) walks one element too far because the loop uses
 * `index <= len`. The checked C+ API makes the off-by-one a defined constraint
 * error when the caller expresses the too-large length: an n of dmax + 1 is
 * rejected, while n == dmax succeeds.
 *
 *   - cplus_memcpy_s(dst, dmax, src, dmax + 1)   -> rejected (ESNOSPC)
 *   - cplus_strncpy_s(dst, dmax, src, dmax + 1)  -> rejected (ESNOSPC)
 *   - the same calls with n == dmax               -> succeed
 *   - the byte just past the bounded region is never written
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_string.h"
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

    /* --- cplus_memcpy_s off-by-one (n = dmax + 1) is rejected --- */
    {
        unsigned char dst[9];
        const unsigned char src[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        memset(dst, 0xAA, sizeof dst);
        CHECK(cplus_memcpy_s(dst, 8, src, 9) == ESNOSPC); /* one too many */
        CHECK(dst[8] == 0xAA);            /* guard byte past dmax intact */
        CHECK(dst[0] == 0 && dst[7] == 0); /* rejected copy zeroes [0,dmax) */
    }

    /* --- cplus_memcpy_s with n == dmax is ok --- */
    {
        unsigned char dst[9];
        memset(dst, 0x00, sizeof dst);
        CHECK(cplus_memcpy_s(dst, 8, "12345678", 8) == 0);
        CHECK(memcmp(dst, "12345678", 8) == 0);
        CHECK(dst[8] == 0x00);            /* guard byte preserved */
    }

    /* --- repeat at a different bound: it is the +1 that is rejected --- */
    {
        unsigned char dst[5];
        memset(dst, 0x11, sizeof dst);
        CHECK(cplus_memcpy_s(dst, 4, "abcd", 5) == ESNOSPC);
        CHECK(dst[4] == 0x11);            /* guard byte past dmax intact */
        CHECK(cplus_memcpy_s(dst, 4, "abcd", 4) == 0);
        CHECK(memcmp(dst, "abcd", 4) == 0);
        CHECK(dst[4] == 0x11);            /* still intact after the good copy */
    }

    /* --- cplus_strncpy_s off-by-one (slen = dmax + 1) is rejected --- */
    {
        char buf[12];
        errno_t r;
        memset(buf, 'Z', sizeof buf);
        /* dmax = 8, slen = 9; src is 8 chars + NUL: no room for the NUL */
        r = cplus_strncpy_s(buf, 8, "abcdefgh", 9);
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        CHECK(buf[8] == 'Z');             /* byte past dmax untouched */
        CHECK(buf[11] == 'Z');
    }

    /* --- cplus_strncpy_s with slen == dmax still fits and succeeds --- */
    {
        char buf[12];
        memset(buf, 'Z', sizeof buf);
        CHECK(cplus_strncpy_s(buf, 8, "abcdefg", 8) == 0); /* 7 chars + NUL */
        CHECK(strcmp(buf, "abcdefg") == 0);
        CHECK(buf[7] == '\0');            /* NUL is the last in-bound byte */
        CHECK(buf[8] == 'Z');             /* guard byte past dmax undisturbed */
    }

    if (failures == 0) {
        printf("bugclass3_offbyone: ok\n");
        return 0;
    }
    printf("bugclass3_offbyone: FAILURES=%d\n", failures);
    return 1;
}
