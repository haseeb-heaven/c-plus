#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_format.h"
#include "safe_runtime.h"

/* Regression: a tracked destination's capacity is the REMAINING bytes from
 * the pointer to the end of its live block.  (size_t)-1 is the untracked
 * sentinel, not a huge capacity, and a remaining value of 0 is a proven
 * zero-byte capacity that must reject every positive dmax. */
int main(void) {
    char *p = (char *)malloc_s(32);
    char stack_buf[64];
    assert(p != NULL);
    cplus_runtime_set_violation_trap(0);

    /* Tracked base: dmax at the capacity is fine, one past it is refused. */
    assert(cplus_safe_remaining_capacity(p) == 32);
    errno = 0;
    assert(cplus_snprintf_s_checked(1, (size_t)-1, p, 32, "%s", "ok") == 2);
    errno = 0;
    assert(cplus_snprintf_s_checked(1, (size_t)-1, p, 33, "%s", "ok") == -1);
    assert(errno == EINVAL);
    assert(strcmp(p, "ok") == 0);

    /* Interior heap pointer: the bound is the remaining bytes (8), not the
     * original block size (32). */
    assert(cplus_safe_remaining_capacity(p + 24) == 8);
    errno = 0;
    assert(cplus_snprintf_s_checked(1, (size_t)-1, p + 24, 8, "%s", "ok") == 2);
    errno = 0;
    assert(cplus_snprintf_s_checked(1, (size_t)-1, p + 24, 9, "%s", "ok") == -1);
    assert(errno == EINVAL);

    /* One-past-the-end: proven capacity 0 refuses any positive dmax. */
    assert(cplus_safe_remaining_capacity(p + 32) == 0);
    errno = 0;
    assert(cplus_snprintf_s_checked(1, (size_t)-1, p + 32, 1, "%s", "ok") == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_snprintf_s_checked(1, (size_t)-1, p + 32, 8, "%s", "ok") == -1);
    assert(errno == EINVAL);
    /* dmax == 0 remains the legal no-write snprintf path. */
    assert(cplus_snprintf_s_checked(1, (size_t)-1, p + 32, 0, "%s", "ok") == 2);

    /* Genuinely unknown capacity keeps the pre-existing permissive policy. */
    errno = 0;
    assert(cplus_snprintf_s(stack_buf, sizeof stack_buf, "%s", "ok") == 2);
    assert(errno == 0);

    cplus_runtime_set_violation_trap(1);
    free_s(p);
    puts("format_dest_bounds: ok");
    return 0;
}
