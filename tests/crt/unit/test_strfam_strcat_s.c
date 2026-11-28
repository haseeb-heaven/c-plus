/*
 * Unit: cplus_strcat_s -- bounded concatenation that preserves termination.
 *
 * Maps to catalogue bug37/bug39: an unchecked strcat can grow the destination
 * past its object and, when dmax is not honoured, overruns the buffer.  The
 * checked adapter must reject the append with a constraint error once the
 * result (including its terminator) no longer fits in dmax.
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
#ifndef ESOVRLP
#define ESOVRLP 404
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

    /* basic append */
    char d[8];
    strcpy(d, "a");
    assert(cplus_strcat_s(d, sizeof d, "bc") == 0);
    assert(strcmp(d, "abc") == 0);
    assert(d[3] == '\0');

    /* exact fit: len(dest) + len(src) + 1 == dmax */
    char e[4];
    strcpy(e, "ab");
    assert(cplus_strcat_s(e, sizeof e, "c") == 0);
    assert(strcmp(e, "abc") == 0);
    assert(e[3] == '\0');

    /* one character too many -> refused, dest cleared */
    strcpy(e, "abc");
    assert(cplus_strcat_s(e, sizeof e, "d") == ESNOSPC);
    assert(e[0] == '\0');

    /* append onto an empty destination */
    d[0] = '\0';
    assert(cplus_strcat_s(d, sizeof d, "hi") == 0);
    assert(strcmp(d, "hi") == 0);

    /* an empty source leaves dest unchanged */
    assert(cplus_strcat_s(d, sizeof d, "") == 0);
    assert(strcmp(d, "hi") == 0);

    /* ---- argument / bound validation ---- */
    assert(cplus_strcat_s(NULL, sizeof d, "x") == ESNULLP);
    assert(cplus_strcat_s(d, sizeof d, NULL) == ESNULLP);
    assert(cplus_strcat_s(d, 0, "x") == ESZEROL);
    assert(cplus_strcat_s(d, (rsize_t)RSIZE_MAX_STR + 1, "x") == ESLEMAX);

    /* destination not terminated within the first dmax bytes */
    char u[5] = { 'a', 'b', 'c', 'd', 'e' };
    assert(cplus_strcat_s(u, 4, "x") == ESUNTERM);

    /* self-append overlaps: a defined constraint violation, not a smash */
    strcpy(d, "ab");
    assert(cplus_strcat_s(d, sizeof d, d) == ESOVRLP);
    assert(cplus_strcat_s(d, 16, d) == ESOVRLP);

    /* ---- guard bytes: nothing is written past dmax ---- */
    struct { char dst[8]; char guard[8]; } g;
    strcpy(g.dst, "ab");
    memset(g.guard, 0x5A, sizeof g.guard);
    assert(cplus_strcat_s(g.dst, 4, "0123456789") == ESNOSPC);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);
    /* exact fit keeps the guard intact */
    strcpy(g.dst, "ab");
    assert(cplus_strcat_s(g.dst, 4, "c") == 0);
    assert(strcmp(g.dst, "abc") == 0);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);

    puts("test_strfam_strcat_s: ok");
    return 0;
}
