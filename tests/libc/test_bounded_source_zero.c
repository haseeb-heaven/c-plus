/* S4 regression: check_src_capacity() must normalize a resolved capacity
 * of zero to UNKNOWN, as source_capacity() already does in bounded.c.
 *
 * Including the implementation exposes the file-static helper so the zero
 * path can be exercised directly, without delegating a one-past-end read to
 * the raw copy loop.  Two deterministic assertions:
 *   1. a tracked block's one-past-the-end address resolves to capacity 0;
 *      that is "no provable size", not a hard bound of zero bytes;
 *   2. a genuine over-read of a proven-capacity source is still refused. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../libc/src/string/bounded.c"

int main(void) {
    unsigned char *p = (unsigned char *)cplus_malloc(8u);
    int failures = 0;
    int rc;
    if (p == NULL) return 2;

    errno = 0;
    rc = check_src_capacity("test", (const void *)(p + 8u), 1u);
    if (rc != 0 || errno != 0) ++failures;

    errno = 0;
    rc = check_src_capacity("test", (const void *)p, 9u);
    if (rc != 1 || errno != ERANGE) ++failures;

    cplus_free(p);
    printf("BOUNDED_SOURCE_ZERO 2 %d\n", failures);
    return failures == 0 ? 0 : 1;
}
