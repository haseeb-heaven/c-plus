/* Unit: sub-object and loop overflow rejection for strcpy_s / strncat_s.
 *
 * Second adversarial catalogue (bug153 writes past a struct member; bug184 loops
 * calling strncat_s with the *total* buffer size each iteration).  The checked
 * API must reject the overflowing copy with a non-zero error instead of writing
 * into the neighbouring member or running off the end of the loop buffer.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- exact fit, then sub-object overflow (bug153) ---- */
    struct { char name[4]; unsigned guard; } rec;
    rec.guard = 0xA5A5A5A5u;
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "bob") == 0);   /* 3 + NUL */
    assert(strcmp(rec.name, "bob") == 0);
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "abc") == 0);   /* exact fit */
    assert(strcmp(rec.name, "abc") == 0);
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "alice") != 0); /* 5 + NUL > 4 */
    assert(rec.guard == 0xA5A5A5A5u);                                /* neighbour safe */
    /* a subsequent in-bounds copy still succeeds: the object stays usable */
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "ok") == 0);
    assert(strcmp(rec.name, "ok") == 0);
    assert(rec.guard == 0xA5A5A5A5u);

    /* ---- argument validation ---- */
    char d[8];
    assert(cplus_strcpy_s(NULL, sizeof d, "hi") != 0);
    assert(cplus_strcpy_s(d, sizeof d, NULL) != 0);
    assert(cplus_strcpy_s(d, 0, "hi") != 0);       /* dmax == 0 */
    assert(cplus_strcpy_s(d, 2, "hi") != 0);       /* needs 3, only 2 */
    assert(cplus_strcpy_s(d, 3, "hi") == 0);       /* 2 + NUL == 3, exact */
    assert(strcmp(d, "hi") == 0);

    /* ---- strncat loop passing the total size each iteration (bug184) ---- */
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
    assert(rejected == 1);                          /* caught, not smashed */

    /* ---- strncat exact fit vs one-over ---- */
    char e[6] = "ab";
    assert(cplus_strncat_s(e, sizeof e, "cde", 3) == 0);   /* "abcde" exact */
    assert(strcmp(e, "abcde") == 0);
    assert(cplus_strncat_s(e, sizeof e, "f", 1) != 0);     /* full + 1 rejected */

    /* ---- strncat truncating copy honours the explicit slen ---- */
    char t[8] = "ab";
    assert(cplus_strncat_s(t, sizeof t, "cdefgh", 3) == 0);
    assert(strcmp(t, "abcde") == 0);                        /* only 3 src chars copied */

    /* ---- more strncat argument validation ---- */
    assert(cplus_strncat_s(NULL, sizeof t, "b", 1) != 0);
    strcpy(t, "abcdefg");                                   /* len 7, dmax 8 exact */
    assert(cplus_strncat_s(t, 8, "x", 1) != 0);             /* no room for NUL */
    /* a rejected strncat_s resets the destination to the empty string; the next
     * check therefore re-seeds it before probing a too-small dmax. */
    assert(t[0] == '\0');
    strcpy(t, "abcdefg");
    assert(cplus_strncat_s(t, 3, "y", 1) != 0);             /* dmax < existing len */

    puts("test_strcpy_strncat_overflow: ok");
    return 0;
}
