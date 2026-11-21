/*
 * Unit: cplus_strcmp_s -- bounded comparison returning the ordering through an
 * explicit indicator pointer rather than an int return value.
 *
 * The comparison is limited to dmax bytes of dest; a constraint violation is
 * reported by the non-zero return code, and on success the sign of *indicator
 * gives the ordering (dest < src, == src, > src).
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

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    int ind = 99;

    /* equal / less / greater */
    assert(cplus_strcmp_s("abc", 8, "abc", &ind) == 0);
    assert(ind == 0);
    assert(cplus_strcmp_s("abc", 8, "abd", &ind) == 0);
    assert(ind < 0);
    assert(cplus_strcmp_s("abd", 8, "abc", &ind) == 0);
    assert(ind > 0);

    /* prefix ordering: the shorter string sorts first */
    assert(cplus_strcmp_s("ab", 8, "abc", &ind) == 0);
    assert(ind < 0);
    assert(cplus_strcmp_s("abc", 8, "ab", &ind) == 0);
    assert(ind > 0);

    /* case sensitivity */
    assert(cplus_strcmp_s("A", 8, "a", &ind) == 0);
    assert(ind < 0);

    /* single-byte difference and equality */
    assert(cplus_strcmp_s("x", 8, "y", &ind) == 0);
    assert(ind < 0);
    assert(cplus_strcmp_s("x", 8, "x", &ind) == 0);
    assert(ind == 0);

    /* empty strings compare equal */
    assert(cplus_strcmp_s("", 8, "", &ind) == 0);
    assert(ind == 0);

    /* difference at the final character */
    assert(cplus_strcmp_s("abz", 8, "abz", &ind) == 0);
    assert(ind == 0);
    assert(cplus_strcmp_s("abz", 8, "aba", &ind) == 0);
    assert(ind > 0);

    /* ---- argument / bound validation ---- */
    assert(cplus_strcmp_s("a", 8, "b", NULL) == ESNULLP);
    assert(cplus_strcmp_s(NULL, 8, "b", &ind) == ESNULLP);
    assert(cplus_strcmp_s("a", 8, NULL, &ind) == ESNULLP);
    assert(cplus_strcmp_s("a", 0, "b", &ind) == ESZEROL);
    assert(cplus_strcmp_s("a", (rsize_t)RSIZE_MAX_STR + 1, "b", &ind) == ESLEMAX);

    /* ---- comparison must not write through its const arguments ---- */
    struct { char dst[8]; char guard[8]; } g;
    strcpy(g.dst, "abc");
    memset(g.guard, 0x5A, sizeof g.guard);
    assert(cplus_strcmp_s(g.dst, 8, "abd", &ind) == 0);
    assert(ind < 0);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);

    puts("test_strfam_strcmp_s: ok");
    return 0;
}
