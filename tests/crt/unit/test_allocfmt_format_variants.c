/* Unit: va_list formatter wrappers (vsnprintf_s / vsprintf_s).
 *
 * Catalogue mapping:
 *   bug81/bug144 - truncation via the v-forms must be detectable and NUL-safe.
 *   bug143       - guard bytes prove the bound is never exceeded.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

static int call_vsn(char *d, size_t n, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = cplus_vsnprintf_s(d, n, f, ap);
    va_end(ap);
    return r;
}

static int call_vsprintf(char *d, size_t n, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = cplus_vsprintf_s(d, n, f, ap);
    va_end(ap);
    return r;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    char buf[32];

    /* ---- fitting render through vsnprintf_s ---- */
    memset(buf, 'Z', sizeof buf);
    assert(call_vsn(buf, sizeof buf, "%s-%d", "ab", 7) == 4);
    assert(strcmp(buf, "ab-7") == 0);
    assert(buf[5] == 'Z');

    /* ---- truncation returns would-be length; bound respected ---- */
    memset(buf, 'Z', sizeof buf);
    assert(call_vsn(buf, 4, "%s", "abcdef") == 6);
    assert(strlen(buf) == 3);
    assert(buf[3] == '\0');
    assert(buf[4] == 'Z');

    /* ---- dmax == 0 / dmax == 1 query and terminator forms ---- */
    memset(buf, 'Z', sizeof buf);
    assert(call_vsn(buf, 0, "%s", "abc") == 3);
    assert(buf[0] == 'Z');
    memset(buf, 'Z', sizeof buf);
    assert(call_vsn(buf, 1, "%s", "abc") == 3);
    assert(buf[0] == '\0' && buf[1] == 'Z');

    /* ---- NULL dest only legal with dmax == 0; NULL format is an error ---- */
    assert(call_vsn(NULL, 0, "%s", "abc") == 3);
    assert(call_vsn(NULL, 8, "%s", "x") == -1);
    errno = 0;
    assert(call_vsn(buf, sizeof buf, NULL) == -1);
    assert(errno == EINVAL);

    /* ---- dest inside a guard array: nothing before or after is touched ---- */
    {
        char g[16];
        memset(g, 'G', sizeof g);
        assert(call_vsn(g + 2, 6, "%s", "abcdefghijk") == 11);   /* needs 11 */
        assert(g[1] == 'G');                 /* before dest */
        assert(strlen(g + 2) == 5);          /* bounded to dmax-1 */
        assert(g[7] == '\0');                /* terminator at dest+dmax-1 */
        assert(g[8] == 'G');                 /* past dmax untouched */
    }

    /* ---- vsprintf_s alias: same bounded contract ---- */
    memset(buf, 'Z', sizeof buf);
    assert(call_vsprintf(buf, sizeof buf, "%d", 42) == 2);
    assert(strcmp(buf, "42") == 0);
    memset(buf, 'Z', sizeof buf);
    assert(call_vsprintf(buf, 4, "%s", "abcdef") == 6);
    assert(strlen(buf) == 3);
    assert(buf[4] == 'Z');
    assert(call_vsprintf(NULL, 0, "%d", 7) == 1);
    assert(call_vsprintf(NULL, 8, "%d", 7) == -1);

    /* ---- wide would-be length from a small bounded buffer ---- */
    memset(buf, 'Z', sizeof buf);
    assert(call_vsn(buf, 8, "%0*d", 20, 5) == 20);
    assert(strlen(buf) == 7);
    assert(buf[8] == 'Z');

    puts("test_allocfmt_format_variants: ok");
    return 0;
}
