/*
 * Bug CLASS: missing NUL terminator / unbounded string read / strcpy overflow.
 *
 * Raw C: a non-NUL-terminated buffer + strlen() reads out of bounds; strcpy()
 * into a small buffer overflows. The checked C+ API is length-bounded:
 *   - cplus_strnlen_s never reads past the supplied bound (returns <= smax).
 *   - cplus_memcmp_s compares at most dmax/smin bytes.
 *   - cplus_strcpy_s rejects a source that does not fit dmax.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNOSPC */

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* deliberately non-NUL-terminated buffer: exactly 5 data bytes */
    char buf[5];
    memcpy(buf, "hello", 5);

    /* bounded length query never over-reads: caps at the bound */
    rsize_t n = cplus_strnlen_s(buf, sizeof buf);
    CHECK(n == 5);
    CHECK(n <= sizeof buf);
    CHECK(cplus_strnlen_s(buf, 3) == 3);       /* smaller bound respected */
    CHECK(cplus_strnlen_s("abc", 8) == 3);     /* NUL found within bound */
    CHECK(cplus_strnlen_s(NULL, 8) == 0);      /* NULL is defined (length 0) */

    /* bounded compare works within the declared bound, no over-read */
    int diff = 99;
    CHECK(cplus_memcmp_s(buf, sizeof buf, "hello", 5, &diff) == 0);
    CHECK(diff == 0);
    CHECK(cplus_memcmp_s(buf, sizeof buf, "hellx", 5, &diff) == 0);
    CHECK(diff != 0);
    /* slen > dmax is a caught constraint violation, not an over-read */
    CHECK(cplus_memcmp_s(buf, sizeof buf, "hello", 6, &diff) != 0);

    /* strcpy_s rejects a source that does not fit the destination */
    char small[4];
    memset(small, 'Z', sizeof small);
    errno_t r = cplus_strcpy_s(small, sizeof small, "toolong");
    CHECK(r != 0);
    CHECK(r == ESNOSPC);
    CHECK(cplus_strncpy_s(small, sizeof small, "toolong", 7) != 0);
    CHECK(cplus_strcpy_s(small, sizeof small, "abc") == 0);
    CHECK(strcmp(small, "abc") == 0);

    /* copy a non-terminated region with the bounded byte API: defined */
    char dst[4];
    CHECK(cplus_memcpy_s(dst, sizeof dst, buf, 4) == 0);
    CHECK(memcmp(dst, "hell", 4) == 0); /* no NUL required, bounded by dmax */

    if (failures == 0) {
        printf("bugclass_string_term: ok\n");
        return 0;
    }
    printf("bugclass_string_term: FAILURES=%d\n", failures);
    return 1;
}
