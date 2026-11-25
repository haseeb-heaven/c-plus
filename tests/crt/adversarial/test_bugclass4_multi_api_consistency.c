/*
 * Bug CLASS 4c: inconsistent enforcement across the checked API surface.
 *
 * For the SAME overflow input (a 10-byte source written into a truthful
 * 6-byte destination region) every checked primitive must give a DEFINED
 * outcome and must never corrupt memory outside the region:
 *
 *   cplus_memcpy_s  -> non-zero failure code (ESNOSPC)
 *   cplus_strcpy_s  -> non-zero failure code (ESNOSPC)
 *   cplus_strncat_s -> non-zero failure code (ESNOSPC)
 *   cplus_snprintf_s-> bounded result (>= 0, here 10 == would-be length),
 *                      valid NUL-terminated prefix, no overrun
 *
 * The destination sits between two guard regions that must stay byte-for-byte
 * intact, proving none of the four APIs writes past its truthful bound. The
 * same four APIs succeed on an exactly-fitting input.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_format.h"
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

static const char *const SRC = "abcdefghij";   /* 10 chars, overflows 6   */

/* true iff p[0..n-1] are all equal to `g` */
static int guards_ok(const unsigned char *p, size_t n, unsigned char g) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != g) return 0;
    return 1;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    const unsigned char G = 0xEE;
    const size_t FRONT = 8;      /* guard before the destination region     */
    const size_t DMAX  = 6;      /* truthful destination bound             */
    const size_t BACK  = 10;     /* guard past the destination region       */

    /* --- memcpy_s: defined failure, guards intact --- */
    {
        unsigned char b[24];
        errno_t r;
        memset(b, G, sizeof b);
        r = cplus_memcpy_s(b + FRONT, DMAX, SRC, 10);
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        CHECK(guards_ok(b, FRONT, G));                       /* front guard  */
        CHECK(guards_ok(b + FRONT + DMAX, BACK, G));         /* back guard    */
    }

    /* --- strcpy_s: defined failure, guards intact --- */
    {
        unsigned char b[24];
        errno_t r;
        memset(b, G, sizeof b);
        r = cplus_strcpy_s((char *)b + FRONT, DMAX, SRC);
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        CHECK(guards_ok(b, FRONT, G));
        CHECK(guards_ok(b + FRONT + DMAX, BACK, G));
    }

    /* --- strncat_s: defined failure, guards intact --- */
    {
        unsigned char b[24];
        errno_t r;
        memset(b, G, sizeof b);
        b[FRONT] = 'q';
        b[FRONT + 1] = '\0';
        r = cplus_strncat_s((char *)b + FRONT, DMAX, SRC, 10);
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        CHECK(guards_ok(b, FRONT, G));
        CHECK(guards_ok(b + FRONT + DMAX, BACK, G));
    }

    /* --- snprintf_s: BOUNDED result (not a hard failure), guards intact --- */
    {
        unsigned char b[24];
        int r;
        size_t i;
        memset(b, G, sizeof b);
        r = cplus_snprintf_s((char *)b + FRONT, DMAX, "%s", SRC);
        CHECK(r >= 0);                       /* bounded result, no error     */
        CHECK(r == 10);                      /* would-be length reported      */
        CHECK(strlen((char *)b + FRONT) < DMAX);  /* valid short prefix       */
        CHECK(b[FRONT + DMAX - 1] == '\0');  /* NUL-terminated within dmax    */
        CHECK(guards_ok(b, FRONT, G));
        CHECK(guards_ok(b + FRONT + DMAX, BACK, G));
        /* the truncated prefix is a clean leading slice of the source */
        for (i = 0; i < strlen((char *)b + FRONT); ++i)
            CHECK(b[FRONT + i] == (unsigned char)SRC[i]);
    }

    /* --- all four succeed on an exactly-fitting input; guards stay intact - */
    {
        unsigned char b[24];

        memset(b, G, sizeof b);
        CHECK(cplus_memcpy_s(b + FRONT, DMAX, "ABCDEF", 6) == 0);
        CHECK(memcmp(b + FRONT, "ABCDEF", 6) == 0);
        CHECK(guards_ok(b, FRONT, G));
        CHECK(guards_ok(b + FRONT + DMAX, BACK, G));

        memset(b, G, sizeof b);
        CHECK(cplus_strcpy_s((char *)b + FRONT, DMAX, "ABCDE") == 0);
        CHECK(strcmp((char *)b + FRONT, "ABCDE") == 0);
        CHECK(guards_ok(b, FRONT, G));
        CHECK(guards_ok(b + FRONT + DMAX, BACK, G));

        memset(b, G, sizeof b);
        b[FRONT] = 'A';
        b[FRONT + 1] = '\0';
        CHECK(cplus_strncat_s((char *)b + FRONT, DMAX, "BCDE", 4) == 0);
        CHECK(strcmp((char *)b + FRONT, "ABCDE") == 0);
        CHECK(guards_ok(b, FRONT, G));
        CHECK(guards_ok(b + FRONT + DMAX, BACK, G));

        memset(b, G, sizeof b);
        CHECK(cplus_snprintf_s((char *)b + FRONT, DMAX, "%s", "ABCDE") == 5);
        CHECK(strcmp((char *)b + FRONT, "ABCDE") == 0);
        CHECK(guards_ok(b, FRONT, G));
        CHECK(guards_ok(b + FRONT + DMAX, BACK, G));
    }

    if (failures == 0) {
        printf("bugclass4_multi_api_consistency: ok\n");
        return 0;
    }
    printf("bugclass4_multi_api_consistency: FAILURES=%d\n", failures);
    return 1;
}
