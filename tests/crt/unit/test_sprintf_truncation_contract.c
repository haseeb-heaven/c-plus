/* Unit: bounded-formatter truncation contract.
 *
 * Second adversarial catalogue (bug143): an off-by-one bounded sprintf must not
 * smash the stack.  The checked formatters guarantee three properties that this
 * test pins down explicitly, for both snprintf_s and the bounded sprintf_s:
 *   (1) never write past `dmax` bytes (the byte at index dmax is untouched);
 *   (2) always NUL-terminate what they do write;
 *   (3) return the length that *would* have been written, so truncation is
 *       detectable by the caller.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    char b[16];

    /* fits exactly: return value is the real length, string is intact */
    memset(b, 'Z', sizeof b);
    assert(cplus_snprintf_s(b, sizeof b, "x%d", 3) == 2);
    assert(strcmp(b, "x3") == 0);

    /* truncation: returns the required length (> dmax), writes only dmax-1 chars */
    memset(b, 'Z', sizeof b);
    int n = cplus_snprintf_s(b, 4, "%s", "abcdefgh");
    assert(n == 8);                         /* would-be length, > dmax */
    assert((int)strlen(b) == 3);            /* bounded to dmax - 1 */
    assert(b[3] == '\0');                   /* NUL-terminated */
    assert(b[4] == 'Z');                    /* byte at index dmax untouched */

    /* dmax == 1: only the terminator is written; required length still returned */
    memset(b, 'Z', sizeof b);
    assert(cplus_snprintf_s(b, 1, "%s", "abc") == 3);
    assert(b[0] == '\0');
    assert(b[1] == 'Z');

    /* dmax == 0 with a valid buffer: nothing written, required length returned */
    memset(b, 'Z', sizeof b);
    assert(cplus_snprintf_s(b, 0, "%s", "abc") == 3);
    assert(b[0] == 'Z');

    /* NULL format is a caught error */
    assert(cplus_snprintf_s(b, sizeof b, NULL) == -1);

    /* NULL destination is only legal in the dmax == 0 "query" form */
    assert(cplus_snprintf_s(NULL, 8, "%s", "x") == -1);
    assert(cplus_snprintf_s(NULL, 0, "%s", "x") == 1);   /* defined, no deref */

    /* guard bytes prove no write lands past the declared bound on truncation */
    char g[8];
    memset(g, 'G', sizeof g);
    assert(cplus_snprintf_s(g + 2, 3, "%s", "abcdef") == 6);
    assert(g[1] == 'G' && g[5] == 'G');
    assert(g[4] == '\0');                   /* terminator at (g + dmax - 1) */

    /* sprintf_s is the same bounded contract (never the unbounded sprintf) */
    memset(b, 'Z', sizeof b);
    n = cplus_sprintf_s(b, 5, "%s", "0123456789");
    assert(n == 10);                        /* required length */
    assert((int)strlen(b) == 4);
    assert(b[4] == '\0');
    assert(b[5] == 'Z');

    /* exact fit: required length == dmax - 1 */
    memset(b, 'Z', sizeof b);
    assert(cplus_sprintf_s(b, 4, "%s", "abc") == 3);
    assert(strcmp(b, "abc") == 0);

    /* sprintf_s invalid dest / NULL-query form mirror snprintf_s */
    assert(cplus_sprintf_s(NULL, 8, "%d", 7) == -1);
    assert(cplus_sprintf_s(NULL, 0, "%d", 7) == 1);

    puts("test_sprintf_truncation_contract: ok");
    return 0;
}
