/*
 * Regression for issue #213 (SafeCRT string scans).
 *
 * The `_auto` wrappers and strcmp_s used the DESTINATION's capacity as the
 * bound for a read from the SOURCE.  Two failure modes followed:
 *
 *   1. a valid short source in a smaller tracked malloc_s buffer was refused
 *      (false runtime violation / _exit(70) on branches that guard scans), or
 *      had its scan refused by the checked backend, which strnlen_s reports as
 *      length 0 -- mistaken by the wrapper for an empty string;
 *   2. strcmp_s could bound a source comparison by a dmax describing dest,
 *     including a dmax larger than the backend's RSIZE_MAX_STR (4096), which
 *     made a valid comparison fail with ESLEMAX (403).
 *
 * The fix clamps every source scan to the source's own proven capacity (and to
 * what the checked backend accepts).  These checks are deterministic: the
 * oversized-dmax comparison returns 403 before the fix and 0 after it, while
 * the valid smaller-source cases must never raise a trap even with violation
 * trapping left at its default (enabled) setting.
 */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../../../stdlib/safe/safe_alloc.h"
#include "../../../../stdlib/safe/safe_log.h"
#include "../../../../stdlib/safe/safe_runtime.h"
#include "../../../../stdlib/safe/safe_string.h"

#ifndef ESLEMAX
#define ESLEMAX 403
#endif

/* Run a case in a child so an unexpected _exit(70) from a false violation is
 * observable instead of killing the test process. */
static int child_status(int (*body)(void)) {
    pid_t pid = fork();
    int st = 0;
    assert(pid >= 0);
    if (pid == 0) {
        int rc = body();
        fflush(NULL);
        _exit(rc == 0 ? 0 : 1);
    }
    assert(waitpid(pid, &st, 0) == pid);
    assert(WIFEXITED(st));
    return WEXITSTATUS(st);
}

/* Headline reproducer: dest(256) with a valid short src in a tracked 16-byte
 * allocation.  Must copy, not abort and not silently copy an empty string. */
static int copy_smaller_src(void) {
    char *dest = (char *)malloc_s(256);
    char *src = (char *)malloc_s(16);
    assert(dest != NULL && src != NULL);
    assert(cplus_strcpy_s(src, 16, "hello") == 0);
    assert(cplus_strcpy_s_auto(dest, src, 256) == dest);
    assert(strcmp(dest, "hello") == 0);
    return 0;
}

/* Same for concatenation: both scans are clamped to their own buffers. */
static int cat_smaller_src(void) {
    char *dest = (char *)malloc_s(256);
    char *src = (char *)malloc_s(16);
    assert(dest != NULL && src != NULL);
    assert(cplus_strcpy_s(dest, 256, "pre") == 0);
    assert(cplus_strcpy_s(src, 16, "hello") == 0);
    assert(cplus_strcat_s_auto(dest, src, 256) == dest);
    assert(strcmp(dest, "prehello") == 0);
    return 0;
}

/* Comparison: dmax describes dest, so it must be clamped to src's capacity
 * before the checked backend sees it.  dmax == 4097 and dmax == RSIZE_MAX_STR+1
 * both exceed the backend's 4096-byte limit yet src holds a valid short
 * string; these are the pre/post-fix discriminating checks. */
static int cmp_smaller_src(void) {
    char *dest = (char *)malloc_s(256);
    char *src = (char *)malloc_s(16);
    int ind = 99;
    assert(dest != NULL && src != NULL);
    assert(cplus_strcpy_s(dest, 256, "hello") == 0);
    assert(cplus_strcpy_s(src, 16, "hello") == 0);

    assert(cplus_strcmp_s(dest, 256, src, &ind) == 0 && ind == 0);
    assert(cplus_strcmp_s(dest, 4097, src, &ind) == 0 && ind == 0);
    assert(cplus_strcmp_s(dest, (rsize_t)RSIZE_MAX_STR + 1u, src, &ind) == 0 &&
           ind == 0);

    assert(cplus_strcpy_s(src, 16, "hellz") == 0);
    assert(cplus_strcmp_s(dest, 4097, src, &ind) == 0 && ind < 0);
    assert(cplus_strcpy_s(src, 16, "hell") == 0);
    assert(cplus_strcmp_s(dest, 4097, src, &ind) == 0 && ind > 0);
    return 0;
}

/* A source with no NUL inside its own tracked capacity must be refused cleanly
 * (NULL + ERANGE) rather than read past the block. */
static int unterminated_smaller_src(void) {
    char *dest = (char *)malloc_s(256);
    char *src = (char *)malloc_s(8);
    assert(dest != NULL && src != NULL);
    memset(src, 'x', 8);
    assert(cplus_strcpy_s(dest, 256, "keep") == 0);

    errno = 0;
    assert(cplus_strcpy_s_auto(dest, src, 256) == NULL);
    assert(errno == ERANGE);
    assert(strcmp(dest, "keep") == 0); /* refused before any write */

    errno = 0;
    assert(cplus_strcat_s_auto(dest, src, 256) == NULL);
    assert(errno == ERANGE);
    assert(strcmp(dest, "keep") == 0);
    return 0;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    assert(cplus_runtime_get_violation_trap() == 1);

    /* Valid calls complete with trapping still enabled: no _exit(70). */
    assert(child_status(copy_smaller_src) == 0);
    assert(child_status(cat_smaller_src) == 0);
    assert(child_status(cmp_smaller_src) == 0);
    assert(child_status(unterminated_smaller_src) == 0);

    /* Trap off: check the exact refusal/control contracts in this process. */
    cplus_runtime_set_violation_trap(0);
    assert(cplus_runtime_get_violation_trap() == 0);
    assert(unterminated_smaller_src() == 0);

    /* A zero-capacity (one-past-the-end) source must never be dereferenced:
     * strcmp_s refuses with ESLEMAX and a zeroed indicator. */
    {
        char *dest = (char *)malloc_s(16);
        char *src = (char *)malloc_s(16);
        char *end = src + 16;
        int ind = 99;
        assert(dest != NULL && src != NULL);
        assert(cplus_strcpy_s(dest, 16, "x") == 0);
        errno = 0;
        assert(cplus_strcmp_s(dest, 16, end, &ind) == ESLEMAX);
        assert(ind == 0 && errno == ERANGE);
        assert(cplus_strcpy_s_auto(dest, end, 16) == NULL);
        assert(errno == ERANGE);
    }

    /* Control: an untracked src does not gain a proven-capacity clamp, so the
     * public oversized-dmax contract stays exactly as before. */
    {
        int ind = 99;
        assert(cplus_strcmp_s("abc", (rsize_t)RSIZE_MAX_STR + 1u, "def", &ind) ==
               ESLEMAX);
        assert(ind == 0);
    }

    puts("test_issue213_source_scan_bound: ok");
    return 0;
}
