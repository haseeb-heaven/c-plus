/* Issue #69: interior heap pointers must be bounded by the REMAINING bytes of
 * their tracked allocation, not just by the exact base address.
 *
 * All four I/O entry points are covered: fread_s, fwrite_s and fgets_s (both
 * the standard-signature wrappers and their _checked macro variants), plus an
 * explicit (size_t)-1 caller bound to prove the sentinel is treated as
 * "untracked" rather than as an unbounded capacity.  There is no cplus_gets_s
 * entry point in this layer, so no gets_s subcase exists to exercise.
 */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_error.h"
#include "safe_runtime.h"

int main(void) {
    char data[64];
    char *buf = (char *)malloc_s(32);
    FILE *rf, *wf;
    size_t i;

    /* Refused checked I/O must return a failure value, not fail-stop. */
    cplus_runtime_set_violation_trap(0);
    assert(buf != NULL);
    memset(buf, 'A', 32);
    for (i = 0; i < sizeof data; ++i) data[i] = (char)('0' + (i % 10));

    rf = tmpfile();
    assert(rf != NULL);
    assert(fwrite(data, 1, sizeof data, rf) == sizeof data);
    rewind(rf);
    wf = tmpfile();
    assert(wf != NULL);

    /* buf + 28 has only 4 readable/writable bytes to the end of the block. */
    assert(cplus_safe_remaining_capacity(buf + 28) == 4);
    assert(cplus_safe_ptr_size(buf + 28) == 0); /* the old, insufficient query */

    /* --- fread_s ------------------------------------------------------- */
    errno = 0;
    assert(cplus_fread_s(buf + 28, 128, 1, 64, rf) == 0);
    assert(errno == EINVAL);
    assert(ftell(rf) == 0); /* refused before any host read */

    errno = 0;
    assert(fread_s(buf + 28, 128, 1, 64, rf) == 0); /* -> _checked variant */
    assert(errno == ERANGE);
    errno = 0;
    assert(cplus_fread_s_checked(buf + 28, (size_t)-1, 128, 1, 64, rf) == 0);
    assert(errno == ERANGE);

    /* Positive control: a bound that fits the 4 remaining bytes succeeds. */
    assert(cplus_fread_s(buf + 28, 4, 1, 4, rf) == 4);
    assert(memcmp(buf + 28, "0123", 4) == 0);
    rewind(rf);

    /* --- fwrite_s ------------------------------------------------------ */
    errno = 0;
    assert(cplus_fwrite_s(buf + 28, 128, 1, 64, wf) == 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(fwrite_s(buf + 28, 128, 1, 64, wf) == 0); /* -> _checked variant */
    assert(errno == ERANGE);
    errno = 0;
    assert(cplus_fwrite_s_checked(buf + 28, (size_t)-1, 128, 1, 64, wf) == 0);
    assert(errno == ERANGE);

    /* Positive control. */
    assert(cplus_fwrite_s(buf + 28, 4, 1, 4, wf) == 4);
    assert(ftell(wf) == 4);

    /* --- fgets_s ------------------------------------------------------- */
    rewind(rf);
    errno = 0;
    assert(cplus_fgets_s(buf + 28, 128, 64, rf) == NULL);
    assert(errno == ERANGE);
    errno = 0;
    assert(fgets_s(buf + 28, 64, rf) == NULL); /* -> _checked variant */
    assert(errno == ERANGE);
    errno = 0;
    assert(cplus_fgets_s_checked(buf + 28, (size_t)-1, 64, rf) == NULL);
    assert(errno == ERANGE);

    /* Positive control: n=4 fits exactly in the 4 remaining bytes. */
    rewind(rf);
    assert(cplus_fgets_s(buf + 28, 4, 4, rf) == buf + 28);
    assert(buf[28] == '0' && buf[31] == '\0');

    /* --- one-past-the-end ---------------------------------------------- */
    /* A proven capacity of 0 must reject EVERY positive request; it is not
     * the same as "unknown capacity" (which defers to the count clamp). */
    rewind(rf);
    assert(cplus_safe_remaining_capacity(buf + 32) == 0);
    errno = 0;
    assert(cplus_fread_s(buf + 32, 1, 1, 1, rf) == 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(fread_s(buf + 32, 1, 1, 1, rf) == 0);
    assert(errno == ERANGE);

    errno = 0;
    assert(cplus_fwrite_s(buf + 32, 1, 1, 1, wf) == 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(fwrite_s(buf + 32, 1, 1, 1, wf) == 0);
    assert(errno == ERANGE);
    assert(ftell(wf) == 4); /* no host write happened */

    errno = 0;
    assert(cplus_fgets_s(buf + 32, 1, 1, rf) == NULL);
    assert(errno == ERANGE);
    errno = 0;
    assert(fgets_s(buf + 32, 1, rf) == NULL);
    assert(errno == ERANGE);

    assert(fclose(rf) == 0);
    assert(fclose(wf) == 0);
    free_s(buf);
    puts("test_io_interior_bounds: ok");
    return 0;
}
