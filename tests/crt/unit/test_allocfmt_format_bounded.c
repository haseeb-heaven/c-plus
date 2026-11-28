/* Unit: bounded formatting - truncation, off-by-one and guard bytes.
 *
 * Catalogue mapping:
 *   bug143       - a bounded sprintf must not run one byte past the buffer.
 *   bug81/bug144 - snprintf-family truncation must be detectable and NUL-safe.
 *   bug51-adjacent: dmax == 0 is a defined "query" form, never a write.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    char buf[32];

    /* ---- sweep every dmax: never write at index dmax, always terminate ---- */
    for (size_t lim = 1; lim <= 16; ++lim) {
        memset(buf, 'G', sizeof buf);
        int n = cplus_snprintf_s(buf, lim, "%s", "abcdefgh");   /* needs 8 */
        assert(n == 8);                                         /* would-be */
        if (lim <= 8) {
            assert(buf[lim - 1] == '\0');                       /* bounded NUL */
            assert(strlen(buf) == lim - 1);                     /* truncated */
        } else {
            assert(strcmp(buf, "abcdefgh") == 0);               /* exact/fit */
            assert(buf[8] == '\0');
        }
        assert(buf[lim] == 'G');      /* one past dmax is untouched (bug143) */
    }

    /* ---- exact fit: dmax == len + 1 ---- */
    memset(buf, 'Z', sizeof buf);
    assert(cplus_snprintf_s(buf, 4, "%s", "abc") == 3);
    assert(strcmp(buf, "abc") == 0);
    assert(buf[4] == 'Z');

    /* ---- one short: dmax == len truncates but reports len (bug143) ---- */
    memset(buf, 'Z', sizeof buf);
    assert(cplus_snprintf_s(buf, 3, "%s", "abc") == 3);
    assert(buf[2] == '\0');
    assert(strlen(buf) == 2);
    assert(buf[3] == 'Z');

    /* ---- dmax == 1: only the terminator is written ---- */
    memset(buf, 'Z', sizeof buf);
    assert(cplus_snprintf_s(buf, 1, "%s", "abc") == 3);
    assert(buf[0] == '\0');
    assert(buf[1] == 'Z');

    /* ---- dmax == 0 with a valid buffer: query form, no write ---- */
    memset(buf, 'Z', sizeof buf);
    assert(cplus_snprintf_s(buf, 0, "%s", "abc") == 3);
    assert(buf[0] == 'Z');

    /* ---- numeric truncation reports the full required length ---- */
    memset(buf, 'Z', sizeof buf);
    assert(cplus_snprintf_s(buf, 4, "%d", 1234567) == 7);
    assert(strlen(buf) == 3);
    assert(buf[4] == 'Z');

    /* ---- bounded sprintf_s is an alias of the same contract (bug143) ---- */
    memset(buf, 'Z', sizeof buf);
    assert(cplus_sprintf_s(buf, 5, "%s", "0123456789") == 10);
    assert(strlen(buf) == 4);
    assert(buf[4] == '\0');
    assert(buf[5] == 'Z');

    /* ---- invalid arguments are caught errors with errno == EINVAL ---- */
    assert(cplus_snprintf_s(buf, sizeof buf, NULL) == -1);
    assert(cplus_snprintf_s(NULL, 8, "%s", "x") == -1);
    errno = 0;
    assert(cplus_sprintf_s(buf, sizeof buf, NULL) == -1);
    assert(errno == EINVAL);

    /* ---- NULL dest is legal only in the dmax == 0 query form ---- */
    assert(cplus_snprintf_s(NULL, 0, "%s", "abc") == 3);
    assert(cplus_sprintf_s(NULL, 8, "%d", 7) == -1);
    assert(cplus_sprintf_s(NULL, 0, "%d", 7) == 1);

    puts("test_allocfmt_format_bounded: ok");
    return 0;
}
