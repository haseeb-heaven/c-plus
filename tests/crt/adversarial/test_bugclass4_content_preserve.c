/*
 * Bug CLASS 4b: a successful checked copy must preserve the EXACT bytes for
 * every in-bounds length, and a copy that would overflow must never report
 * success.
 *
 *   - cplus_memcpy_s over all lengths 0..dmax: returns 0, copies exactly n
 *     bytes, leaves every byte past n untouched; n == 0 is a defined no-op.
 *   - cplus_strcpy_s over all string lengths 0..dmax-1: returns 0, the string
 *     compares equal, and no slack byte past the NUL is overwritten.
 *   - overflowing memcpy/strcpy/strncpy/strncat calls all return non-zero.
 *   - a bounded formatter never writes past dmax and always terminates.
 *
 * The caller passes a truthful dmax in every case.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

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

    /* --- memcpy_s preserves exact bytes for ALL lengths 0..dmax --- */
    {
        unsigned char dst[8];
        const unsigned char src[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };
        int n;
        for (n = 0; n <= 8; ++n) {
            errno_t r;
            memset(dst, 0x77, sizeof dst);
            r = cplus_memcpy_s(dst, sizeof dst, src, (rsize_t)n);
            CHECK(r == 0);
            if (n == 0) {
                /* slen == 0 is a defined no-op: nothing is written */
                unsigned char ref[8];
                memset(ref, 0x77, sizeof ref);
                CHECK(memcmp(dst, ref, sizeof dst) == 0);
            } else {
                CHECK(memcmp(dst, src, (size_t)n) == 0); /* exact bytes     */
                if (n < 8) CHECK(dst[n] == 0x77);        /* rest untouched  */
            }
        }
    }

    /* --- strcpy_s preserves exact bytes for ALL lengths 0..dmax-1 --- */
    {
        char dst[8];
        char src[8];
        int L;
        for (L = 0; L < 8; ++L) {
            int i;
            for (i = 0; i < L; ++i) src[i] = (char)('A' + i);
            src[L] = '\0';
            memset(dst, 0x7E, sizeof dst);
            CHECK(cplus_strcpy_s(dst, sizeof dst, src) == 0);
            CHECK(strcmp(dst, src) == 0);            /* exact string         */
            CHECK((int)strlen(dst) == L);
            if (L + 1 < 8) CHECK(dst[L + 1] == 0x7E); /* slack not rewritten */
        }
    }

    /* --- a rejected (overflowing) copy NEVER returns success --- */
    {
        unsigned char dst[8];
        const unsigned char big[16] = {
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
        };
        memset(dst, 0x33, sizeof dst);
        CHECK(cplus_memcpy_s(dst, 4, big, 16) != 0);           /* memcpy    */
        CHECK(cplus_strcpy_s((char *)dst, 4, "abcdefghij") != 0); /* strcpy */
        CHECK(cplus_strncpy_s((char *)dst, 4, "abcdefghij", 10) != 0);
        memset(dst, 0x00, sizeof dst);
        dst[0] = 'a';
        dst[1] = 'b';
        dst[2] = '\0';
        CHECK(cplus_strncat_s((char *)dst, 4, "0123456789", 10) != 0);
    }

    /* --- a bounded formatter is bounded and terminated, never overruns --- */
    {
        char buf[10];
        size_t i;
        int r;
        memset(buf, 0x5A, sizeof buf);
        r = cplus_snprintf_s(buf, 6, "%s", "abcdefghij"); /* 10 > dmax 6     */
        CHECK(r >= 0);                 /* not a hard error: bounded result    */
        CHECK(r == 10);                /* reports the full required length    */
        CHECK((int)strlen(buf) <= 5);  /* bounded to a valid prefix           */
        CHECK(buf[5] == '\0');         /* NUL-terminated at/under dmax        */
        for (i = 6; i < 10; ++i) CHECK(buf[i] == 0x5A); /* no overrun        */
    }

    if (failures == 0) {
        printf("bugclass4_content_preserve: ok\n");
        return 0;
    }
    printf("bugclass4_content_preserve: FAILURES=%d\n", failures);
    return 1;
}
