/*
 * Bug CLASS: overlapping copy with the wrong primitive.
 *
 * Raw C: memcpy(dest, src, n) with overlapping regions is undefined. The checked
 * C+ API catches it: cplus_memcpy_s rejects an offset overlap with the defined
 * constraint error ESOVRLP (no partial/garbled copy), while cplus_memmove_s
 * handles the same overlap correctly.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESOVRLP */

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

    /* forward overlap -> memcpy_s rejects with ESOVRLP */
    {
        char data[16] = "0123456789";
        errno_t r = cplus_memcpy_s(data + 2, sizeof data - 2, data, 5);
        CHECK(r != 0);
        CHECK(r == ESOVRLP);
    }

    /* backward overlap -> also rejected */
    {
        char data[16] = "0123456789";
        errno_t r = cplus_memcpy_s(data, sizeof data, data + 2, 5);
        CHECK(r != 0);
        CHECK(r == ESOVRLP);
    }

    /* dest == src is explicitly allowed (not treated as overlap) */
    {
        char same[16] = "abcdef";
        CHECK(cplus_memcpy_s(same, sizeof same, same, 6) == 0);
        CHECK(strcmp(same, "abcdef") == 0);
    }

    /* non-overlapping regions still copy normally */
    {
        char a[8] = "aaaaaa";
        const char b[8] = "bbbbbb";
        CHECK(cplus_memcpy_s(a, sizeof a, b, 6) == 0);
        CHECK(strcmp(a, "bbbbbb") == 0);
    }

    /* memmove_s handles the SAME overlaps that memcpy_s rejects */
    {
        char mv[16] = "0123456789";
        CHECK(cplus_memmove_s(mv + 2, sizeof mv - 2, mv, 5) == 0);
        CHECK(memcmp(mv, "0101234789", 10) == 0);
    }
    {
        char mv[16] = "0123456789";
        CHECK(cplus_memmove_s(mv, sizeof mv, mv + 2, 5) == 0);
        CHECK(memcmp(mv, "2345656789", 10) == 0);
    }

    /* memmove_s also rejects a length that exceeds dmax */
    {
        char mv[8] = "abc";
        CHECK(cplus_memmove_s(mv, sizeof mv, "abcdefghij", 10) != 0);
    }

    if (failures == 0) {
        printf("bugclass_overlap: ok\n");
        return 0;
    }
    printf("bugclass_overlap: FAILURES=%d\n", failures);
    return 1;
}
