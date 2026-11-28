/*
 * Unit: cplus_strcpy_s -- bounded copy that always NUL-terminates.
 *
 * Maps to catalogue bug37/bug39: the classic C strcpy has no destination bound
 * (it writes past the end of a too-small buffer) and can leave the destination
 * without a terminator.  The checked adapter must reject an over-long source
 * with a constraint error instead of overrunning, and every accepted copy must
 * write exactly one terminating NUL inside dmax.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"

/* safeclib constraint codes are positive (see safe_lib_errno.h). */
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

    /* ---- success: copies src and writes the terminating NUL ---- */
    char d[8];
    assert(cplus_strcpy_s(d, sizeof d, "hi") == 0);
    assert(strcmp(d, "hi") == 0);
    assert(d[2] == '\0');

    /* exact fit: strlen(src) + 1 == dmax */
    char e[3];
    assert(cplus_strcpy_s(e, sizeof e, "ab") == 0);
    assert(strcmp(e, "ab") == 0);
    assert(e[2] == '\0');

    /* empty source is a valid, terminator-only result */
    assert(cplus_strcpy_s(d, sizeof d, "") == 0);
    assert(d[0] == '\0');

    /* ---- bug37/39: an over-long source is refused, not truncated/overrun ---- */
    char small[4];
    small[0] = 'X';
    assert(cplus_strcpy_s(small, sizeof small, "abcdef") == ESNOSPC);
    assert(small[0] == '\0');                    /* dest cleared on violation */
    assert(cplus_strcpy_s(small, sizeof small, "abcd") == ESNOSPC); /* 4+NUL>4 */
    assert(small[0] == '\0');
    /* the same small object accepts a fitting value afterwards */
    assert(cplus_strcpy_s(small, sizeof small, "abc") == 0);
    assert(strcmp(small, "abc") == 0);

    /* ---- argument / bound validation ---- */
    assert(cplus_strcpy_s(NULL, sizeof d, "hi") == ESNULLP);
    assert(cplus_strcpy_s(d, sizeof d, NULL) == ESNULLP);
    assert(cplus_strcpy_s(d, 0, "hi") == ESZEROL);
    assert(cplus_strcpy_s(d, (rsize_t)RSIZE_MAX_STR + 1, "hi") == ESLEMAX);

    /* self copy is a defined no-op */
    strcpy(d, "hi");
    assert(cplus_strcpy_s(d, sizeof d, d) == 0);
    assert(strcmp(d, "hi") == 0);

    /* ---- guard bytes: nothing is written past dmax ---- */
    struct { char dst[8]; char guard[8]; } g;
    memset(g.guard, 0x5A, sizeof g.guard);
    g.dst[4] = 'Q';                              /* canary just past dmax=4 */
    assert(cplus_strcpy_s(g.dst, 4, "0123456789") == ESNOSPC);
    assert(g.dst[4] == 'Q');
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);
    /* an in-bounds copy afterwards keeps the guard intact */
    assert(cplus_strcpy_s(g.dst, sizeof g.dst, "ok") == 0);
    assert(strcmp(g.dst, "ok") == 0);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);

    puts("test_strfam_strcpy_s: ok");
    return 0;
}
