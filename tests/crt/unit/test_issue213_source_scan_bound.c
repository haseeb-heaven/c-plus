/*
 * Regression for issue #213: the _auto string wrappers and strcmp_s used the
 * DESTINATION's capacity as the scan ceiling for the SOURCE.  A valid short
 * source living in a smaller tracked malloc_s buffer was therefore refused
 * (trap => _exit(70)), or, with trapping disabled, misread as a 0-length
 * string.  These checks derive the source scan bound from the source's own
 * proven capacity while keeping the read-side bound and refused-empty
 * semantics intact.
 */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "safe_alloc.h"
#include "safe_log.h"
#include "safe_runtime.h"
#include "safe_string.h"

/* A false violation calls cplus_runtime_violation() -> _exit(70).  Run each
 * trap-enabled case in a child so the parent can observe that exact exit code
 * without turning the trap off in the process under test. */
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

static int copy_smaller_src(void) {
    char *dest = (char *)malloc_s(256);
    char *src = (char *)malloc_s(16);
    assert(dest != NULL && src != NULL);
    assert(cplus_strcpy_s(src, 16, "hello") == 0);
    assert(cplus_strcpy_s_auto(dest, src, 256) == dest);
    assert(strcmp(dest, "hello") == 0);
    return 0;
}

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

static int cmp_smaller_src(void) {
    char *dest = (char *)malloc_s(256);
    char *src = (char *)malloc_s(16);
    int ind = 99;
    assert(dest != NULL && src != NULL);
    assert(cplus_strcpy_s(dest, 256, "hello") == 0);
    assert(cplus_strcpy_s(src, 16, "hello") == 0);
    assert(cplus_strcmp_s(dest, 256, src, &ind) == 0 && ind == 0);
    assert(cplus_strcpy_s(src, 16, "hellz") == 0);
    assert(cplus_strcmp_s(dest, 256, src, &ind) == 0 && ind < 0);
    assert(cplus_strcpy_s(src, 16, "hell") == 0);
    assert(cplus_strcmp_s(dest, 256, src, &ind) == 0 && ind > 0);
    return 0;
}

/* An unterminated source must be refused cleanly, never read past its own
 * capacity and never reported as a runtime violation. */
static int unterminated_smaller_src(void) {
    char *dest = (char *)malloc_s(256);
    char *src = (char *)malloc_s(8);
    assert(dest != NULL && src != NULL);
    memset(src, 'x', 8); /* no NUL within src's proven capacity */
    assert(cplus_strcpy_s(dest, 256, "keep") == 0);
    errno = 0;
    assert(cplus_strcpy_s_auto(dest, src, 256) == NULL);
    assert(errno == ERANGE);
    assert(dest[0] == '\0'); /* refused-empty semantics preserved */
    return 0;
}

/* Control: the public bounded query must still refuse an oversized scan. */
static int public_oversized_scan(void) {
    char *src = (char *)malloc_s(16);
    assert(src != NULL);
    assert(cplus_strnlen_s(src, 256) == 0); /* must not be reached */
    return 0;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    assert(cplus_runtime_get_violation_trap() == 1);

    /* Trap enabled: valid calls with a smaller tracked source complete. */
    assert(child_status(copy_smaller_src) == 0);
    assert(child_status(cat_smaller_src) == 0);
    assert(child_status(cmp_smaller_src) == 0);
    assert(child_status(unterminated_smaller_src) == 0);

    /* Trap still works for a genuinely out-of-range public scan. */
    assert(child_status(public_oversized_scan) == 70);

    /* Same cases with trapping off: check the exact contracts. */
    cplus_runtime_set_violation_trap(0);
    {
        char *dest = (char *)malloc_s(256);
        char *src = (char *)malloc_s(16);
        int ind = 99;
        assert(dest != NULL && src != NULL);
        assert(cplus_strcpy_s(src, 16, "hello") == 0);
        assert(cplus_strnlen_s(src, 16) == 5); /* source-derived bound works */

        errno = 0;
        assert(cplus_strcpy_s_auto(dest, src, 256) == dest && errno == 0);
        assert(strcmp(dest, "hello") == 0);

        assert(cplus_strcpy_s(dest, 256, "pre") == 0);
        errno = 0;
        assert(cplus_strcat_s_auto(dest, src, 256) == dest && errno == 0);
        assert(strcmp(dest, "prehello") == 0);

        errno = 0;
        assert(cplus_strcmp_s(dest, 256, src, &ind) == 0 && errno == 0);
        assert(ind > 0); /* "prehello" > "hello" */

        /* Interior pointer: remaining capacity of the tracked block bounds
         * the source scan, not the containing block's full size. */
        {
            char *block = (char *)malloc_s(32);
            char *interior;
            assert(block != NULL);
            interior = block + 16;
            strcpy(interior, "hi");
            assert(cplus_strcpy_s_auto(dest, interior, 256) == dest);
            assert(strcmp(dest, "hi") == 0);
            free_s(block);
        }
        free_s(src);
        free_s(dest);
    }

    /* The public oversized scan is still a refusal, not a silent 0. */
    {
        char *src = (char *)malloc_s(16);
        assert(src != NULL);
        errno = 0;
        assert(cplus_strnlen_s(src, 256) == 0 && errno == ERANGE);
        free_s(src);
    }

    cplus_runtime_set_violation_trap(1);
    assert(cplus_runtime_get_violation_trap() == 1);
    puts("test_issue213_source_scan_bound: ok");
    return 0;
}
