/*
 * Unit: cplus_strncat_s -- bounded append of at most slen source bytes.
 *
 * Maps to catalogue bug184: a loop that appends with the *total* buffer size as
 * dmax on every iteration overflows an unchecked strncat once the buffer fills.
 * The checked call must instead return a non-zero constraint error at the
 * iteration that no longer fits, and never write past the object.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"

#ifndef ESNULLP
#define ESNULLP 400
#endif
#ifndef ESZEROL
#define ESZEROL 401
#endif
#ifndef ESLEMAX
#define ESLEMAX 403
#endif
#ifndef ESNOSPC
#define ESNOSPC 406
#endif
#ifndef ESUNTERM
#define ESUNTERM 407
#endif

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* basic bounded append */
    char d[8];
    strcpy(d, "a");
    assert(cplus_strncat_s(d, sizeof d, "bcdef", 2) == 0);
    assert(strcmp(d, "abc") == 0);
    assert(d[3] == '\0');

    /* only slen characters are taken from src */
    char t[8];
    strcpy(t, "ab");
    assert(cplus_strncat_s(t, sizeof t, "cdefgh", 3) == 0);
    assert(strcmp(t, "abcde") == 0);
    assert(t[5] == '\0');

    /* exact fit: len(dest) + slen + 1 == dmax */
    char e[6];
    strcpy(e, "ab");
    assert(cplus_strncat_s(e, sizeof e, "cde", 3) == 0);
    assert(strcmp(e, "abcde") == 0);
    assert(e[5] == '\0');

    /* one byte too many -> refused, dest cleared */
    assert(cplus_strncat_s(e, sizeof e, "f", 1) == ESNOSPC);
    assert(e[0] == '\0');

    /* slen == 0: msvcrt-analog EOK when dest fits (dest is cleared), ESZEROL
       when dest does not fit within dmax */
    strcpy(d, "abc");
    assert(cplus_strncat_s(d, sizeof d, "xyz", 0) == 0);
    assert(d[0] == '\0');
    char z[4] = "abc";                           /* no NUL within dmax = 3 */
    assert(cplus_strncat_s(z, 3, "x", 0) == ESZEROL);

    /* ---- argument / bound validation ---- */
    assert(cplus_strncat_s(NULL, sizeof d, "x", 1) == ESNULLP);
    assert(cplus_strncat_s(d, sizeof d, NULL, 1) == ESNULLP);
    assert(cplus_strncat_s(d, 0, "x", 1) == ESZEROL);
    assert(cplus_strncat_s(d, sizeof d, "x", (rsize_t)RSIZE_MAX_STR + 1) == ESLEMAX);

    char u[5] = { 'a', 'b', 'c', 'd', 'e' };
    assert(cplus_strncat_s(u, 4, "x", 1) == ESUNTERM);

    /* ---- bug184: append loop reusing the total buffer size each pass ---- */
    char loop[16];
    assert(cplus_strcpy_s(loop, sizeof loop, "") == 0);
    const char *parts[] = { "alpha", "beta", "gamma", "delta" };
    int rejected = 0;
    for (int i = 0; i < 4; ++i) {
        if (cplus_strncat_s(loop, sizeof loop, parts[i], sizeof loop) != 0) {
            rejected = 1;
            break;
        }
        assert(strlen(loop) <= sizeof loop - 1);   /* never past the bound */
    }
    assert(rejected == 1);                         /* caught, not smashed */
    assert(strlen(loop) <= sizeof loop - 1);

    /* ---- guard bytes: nothing is written past dmax ---- */
    struct { char dst[8]; char guard[8]; } g;
    strcpy(g.dst, "ab");
    memset(g.guard, 0x5A, sizeof g.guard);
    assert(cplus_strncat_s(g.dst, 4, "0123456789", 10) == ESNOSPC);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);
    strcpy(g.dst, "ab");
    assert(cplus_strncat_s(g.dst, 4, "c", 1) == 0);
    assert(strcmp(g.dst, "abc") == 0);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);

    puts("test_strfam_strncat_s: ok");
    return 0;
}
