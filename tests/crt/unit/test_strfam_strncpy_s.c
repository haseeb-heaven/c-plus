/*
 * Unit: cplus_strncpy_s -- bounded copy that always NUL-terminates.
 *
 * Maps to catalogue bug38: C strncpy does not NUL-terminate when the source is
 * at least n bytes long, so a truncated copy leaves dest non-terminated and the
 * next read runs off the end.  strncpy_s must either terminate the copy or
 * refuse with a constraint error -- never hand back a non-terminated string.
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

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* truncating copy bounded by slen; result is NUL-terminated (bug38) */
    char d[8];
    memset(d, 'Z', sizeof d);
    assert(cplus_strncpy_s(d, sizeof d, "abcdef", 3) == 0);
    assert(strcmp(d, "abc") == 0);
    assert(d[3] == '\0');

    /* exact fit: slen == strlen(src), dmax == slen + 1 */
    char e[4];
    memset(e, 'Z', sizeof e);
    assert(cplus_strncpy_s(e, sizeof e, "abc", 3) == 0);
    assert(strcmp(e, "abc") == 0);
    assert(e[3] == '\0');

    /* src shorter than slen: copy stops at the src NUL and terminates */
    memset(d, 'Z', sizeof d);
    assert(cplus_strncpy_s(d, sizeof d, "ab", 100) == 0);
    assert(strcmp(d, "ab") == 0);
    assert(d[2] == '\0');

    /* slen == 0 clears the destination (defined) */
    strcpy(d, "junk");
    assert(cplus_strncpy_s(d, sizeof d, "abc", 0) == 0);
    assert(d[0] == '\0');

    /* bug38: no room for the terminator -> refused, not left unterminated */
    char small[3];
    small[0] = 'X';
    assert(cplus_strncpy_s(small, sizeof small, "abc", 10) == ESNOSPC);
    assert(small[0] == '\0');

    /* ---- argument / bound validation ---- */
    assert(cplus_strncpy_s(NULL, sizeof d, "a", 1) == ESNULLP);
    assert(cplus_strncpy_s(d, sizeof d, NULL, 3) == ESNULLP);
    assert(cplus_strncpy_s(d, 0, "a", 1) == ESZEROL);
    assert(cplus_strncpy_s(d, sizeof d, "a", (rsize_t)RSIZE_MAX_STR + 1) == ESLEMAX);
    assert(cplus_strncpy_s(d, (rsize_t)RSIZE_MAX_STR + 1, "a", 1) == ESLEMAX);

    /* ---- guard bytes: nothing is written past dmax ---- */
    struct { char dst[8]; char guard[8]; } g;
    memset(g.guard, 0x5A, sizeof g.guard);
    g.dst[4] = 'Q';                              /* canary just past dmax=4 */
    assert(cplus_strncpy_s(g.dst, 4, "0123456789", 12) == ESNOSPC);
    assert(g.dst[4] == 'Q');
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);
    /* a fitting truncated copy still succeeds and keeps the guard intact */
    assert(cplus_strncpy_s(g.dst, 4, "0123456789", 2) == 0);
    assert(strcmp(g.dst, "01") == 0);
    assert(g.dst[2] == '\0');
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);

    puts("test_strfam_strncpy_s: ok");
    return 0;
}
