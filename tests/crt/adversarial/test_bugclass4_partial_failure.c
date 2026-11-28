/*
 * Bug CLASS 4a: partial failure of a checked copy -- after the C+ safe API
 * rejects an out-of-bounds copy, the destination must be left in a DEFINED
 * state (C11 memcpy_s/strcpy_s contract) and the bytes past the truthful
 * destination bound (dmax) must be untouched. A retry with the correct bound
 * must succeed.
 *
 *   memcpy_s reject  -> destination prefix [0,dmax) is ZEROED, guard intact
 *   strcpy_s reject  -> destination becomes an EMPTY string (NUL at [0]),
 *                       guard past dmax intact
 *   strncat_s reject -> destination is CLEARED (NUL at [0]), guard intact
 *   NULL src reject  -> destination prefix [0,dmax) is ZEROED, guard intact
 *   retry w/ correct bounds -> succeeds and produces the exact bytes
 *
 * The caller always passes a TRUTHFUL dmax (the real size of the bounded
 * region), so a rejection is proof the adapter caught a real overrun.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNOSPC, ESNULLP */

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

    /* --- memcpy_s rejected: zeroed prefix + guards past dmax intact --- */
    {
        unsigned char arena[16];
        const unsigned char src[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        const unsigned char sentinel = 0xA5;
        size_t i;
        errno_t r;

        memset(arena, sentinel, sizeof arena);
        r = cplus_memcpy_s(arena, 6, src, 10);       /* 10 > truthful dmax 6 */
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        for (i = 0; i < 6; ++i) CHECK(arena[i] == sentinel); /* [0,dmax) intact */
        for (i = 6; i < 16; ++i) CHECK(arena[i] == sentinel); /* guard intact */

        /* retry with the correct bound: succeeds, exact bytes land */
        memset(arena, 0x00, sizeof arena);
        CHECK(cplus_memcpy_s(arena, sizeof arena, src, sizeof src) == 0);
        CHECK(memcmp(arena, src, sizeof src) == 0);
        CHECK(arena[10] == 0x00);                     /* byte past copy kept */
    }

    /* --- strcpy_s rejected: NUL-sized empty destination, guard intact --- */
    {
        unsigned char buf[16];
        const unsigned char sentinel = 0xB6;
        size_t i;
        errno_t r;

        memset(buf, sentinel, sizeof buf);
        r = cplus_strcpy_s((char *)buf, 6, "0123456789"); /* src len 10 > 6 */
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        CHECK(buf[0] == 0);                           /* C11: dest == ""     */
        CHECK(strlen((char *)buf) == 0);
        for (i = 6; i < 16; ++i) CHECK(buf[i] == sentinel);

        /* retry with the correct bound */
        memset(buf, 0x00, sizeof buf);
        CHECK(cplus_strcpy_s((char *)buf, sizeof buf, "0123456789") == 0);
        CHECK(strcmp((char *)buf, "0123456789") == 0);
    }

    /* --- strncat_s rejected: destination cleared, guard intact --- */
    {
        unsigned char buf[16];
        const unsigned char sentinel = 0xC7;
        size_t i;
        errno_t r;

        memset(buf, sentinel, sizeof buf);
        buf[0] = 'X';
        buf[1] = '\0';
        r = cplus_strncat_s((char *)buf, 6, "0123456789", 10);
        CHECK(r != 0);
        CHECK(buf[0] == 0);                           /* dest cleared        */
        CHECK(strlen((char *)buf) == 0);
        for (i = 6; i < 16; ++i) CHECK(buf[i] == sentinel);

        /* retry with the correct bound: exact concatenation */
        memset(buf, 0x00, sizeof buf);
        buf[0] = 'X';
        buf[1] = '\0';
        CHECK(cplus_strncat_s((char *)buf, sizeof buf, "abcdefghij", 10) == 0);
        CHECK(strcmp((char *)buf, "Xabcdefghij") == 0);
    }

    /* --- NULL src reject: destination prefix cleared, guard intact --- */
    {
        unsigned char arena[12];
        const unsigned char sentinel = 0xD8;
        size_t i;
        errno_t r;

        memset(arena, sentinel, sizeof arena);
        r = cplus_memcpy_s(arena, 6, NULL, 3);
        CHECK(r != 0);
        CHECK(r == ESNULLP);
        for (i = 0; i < 6; ++i) CHECK(arena[i] == sentinel);
        for (i = 6; i < 12; ++i) CHECK(arena[i] == sentinel);

        /* retry with a real source and correct bounds */
        CHECK(cplus_memcpy_s(arena, sizeof arena, "abcdef", 6) == 0);
        CHECK(memcmp(arena, "abcdef", 6) == 0);
    }

    if (failures == 0) {
        printf("bugclass4_partial_failure: ok\n");
        return 0;
    }
    printf("bugclass4_partial_failure: FAILURES=%d\n", failures);
    return 1;
}
