/*
 * Unit: cplus_strnlen_s -- bounded length query that never reads past smax.
 *
 * Unlike the errno_t members this returns a count in [0, smax].  A NULL pointer
 * or an out-of-range smax yields 0; a string without a NUL inside the first smax
 * bytes yields smax.  It is a read-only query.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ordinary bounded lengths */
    assert(cplus_strnlen_s("abc", 10) == 3);
    assert(cplus_strnlen_s("abc", 3) == 3);
    assert(cplus_strnlen_s("abc", 2) == 2);
    assert(cplus_strnlen_s("abc", 1) == 1);

    /* empty string / degenerate smax / NULL pointer */
    assert(cplus_strnlen_s("", 5) == 0);
    assert(cplus_strnlen_s("", 1) == 0);
    assert(cplus_strnlen_s("abc", 0) == 0);
    assert(cplus_strnlen_s(NULL, 10) == 0);
    assert(cplus_strnlen_s("abc", (rsize_t)RSIZE_MAX_STR + 1) == 0);

    /* buffer with no NUL inside the scanned window */
    char nx[6] = { 'a', 'b', 'c', 'd', 'e', 'f' };
    assert(cplus_strnlen_s(nx, 6) == 6);
    assert(cplus_strnlen_s(nx, 3) == 3);
    assert(cplus_strnlen_s(nx, 1) == 1);

    /* NUL inside the window bounds the count */
    char e[6] = { 'a', 'b', '\0', 'x', 'y', 'z' };
    assert(cplus_strnlen_s(e, 6) == 2);
    assert(cplus_strnlen_s(e, 2) == 2);
    assert(cplus_strnlen_s(e, 1) == 1);

    /* agrees with strlen for a terminated string */
    const char *s = "the quick brown fox";
    assert(cplus_strnlen_s(s, sizeof "the quick brown fox") == strlen(s));
    assert(cplus_strnlen_s(s, strlen(s)) == strlen(s));
    assert(cplus_strnlen_s(s, strlen(s) - 1) == strlen(s) - 1);

    /* ---- read-only: bytes past smax are untouched ---- */
    struct { char dst[6]; char guard[8]; } g;
    memcpy(g.dst, nx, sizeof g.dst);
    memset(g.guard, 0x5A, sizeof g.guard);
    assert(cplus_strnlen_s(g.dst, sizeof g.dst) == sizeof g.dst);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);

    puts("test_strfam_strnlen_s: ok");
    return 0;
}
