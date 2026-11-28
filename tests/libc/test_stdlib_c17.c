/* Standalone C99 test for the PLATFORM-BACKED ISO C17 stdlib wrappers.
 * Every symbol is declared locally: this test does not depend on the installed
 * public header. Memory from cplus_aligned_alloc belongs to the HOST CRT and is
 * intentionally released with the host free() on POSIX/macOS. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Local declarations of the namespaced wrappers under test. */
void cplus_libc_log_set_enabled(int enabled);
void *cplus_aligned_alloc(size_t alignment, size_t size);
void cplus_quick_exit(int status);
int cplus_at_quick_exit(void (*func)(void));
void cplus__Exit(int status);

/* Harmless quick-exit handler registered in the parent; the children inherit
 * the registration but the handler only touches a local static counter. */
static void c17_quick_exit_handler(void) {
    static volatile unsigned int calls = 0u;
    ++calls;
}

#ifndef _WIN32
static void test_termination_paths(void) {
    pid_t pid;
    int status = 0;

    assert(cplus_at_quick_exit(c17_quick_exit_handler) == 0);

    /* quick_exit() must terminate the child with exactly the requested status.
     * It is never invoked in the parent. */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        cplus_quick_exit(42);
        _exit(99); /* unreachable safety net if the host call ever returned */
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 42);

    /* _Exit() must terminate the child with exactly the requested status. */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        cplus__Exit(17);
        _exit(99); /* unreachable safety net */
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 17);
}
#endif /* !_WIN32 */

int main(void) {
    /* 1. Logging enabled/disabled must not change any return value or the
     *    usability of the returned block. Run the positive and the first
     *    invalid case under BOTH logging states. */
    for (int enabled = 0; enabled <= 1; ++enabled) {
        void *p;
        unsigned char *bytes;
        size_t i;

        cplus_libc_log_set_enabled(enabled);

        p = cplus_aligned_alloc(64u, 128u);
        assert(p != NULL);
        assert(((uintptr_t)p % (uintptr_t)64u) == 0u);
        bytes = (unsigned char *)p;
        for (i = 0u; i < 128u; ++i) bytes[i] = (unsigned char)(i ^ 0xA5u);
        for (i = 0u; i < 128u; ++i) assert(bytes[i] == (unsigned char)(i ^ 0xA5u));
        free(p); /* host CRT owns this pointer; never cplus_free() */

        errno = 0;
        assert(cplus_aligned_alloc(3u, 128u) == NULL); /* 3 is not a power of two */
        assert(errno == EINVAL);
    }
    cplus_libc_log_set_enabled(0);

    /* 2. Remaining constraint violations: alignment smaller than sizeof(void *)
     *    and a size that is not an integral multiple of the alignment. */
    {
        size_t small = sizeof(void *) / 2u; /* power of two but < sizeof(void *) */
        errno = 0;
        assert(cplus_aligned_alloc(small, small * 2u) == NULL);
        assert(errno == EINVAL);

        errno = 0;
        assert(cplus_aligned_alloc(64u, 100u) == NULL); /* 100 % 64 != 0 */
        assert(errno == EINVAL);
    }

    /* 3. Extreme inputs must be rejected by the constraint guard without
     *    crashing or attempting an enormous allocation. */
    errno = 0;
    assert(cplus_aligned_alloc(SIZE_MAX, SIZE_MAX) == NULL);

#ifndef _WIN32
    /* 4. Process-termination wrappers (POSIX fork/wait). Windows has no fork(),
     *    so this section is guarded and documented rather than faked. */
    test_termination_paths();
#else
    /* On Windows only the registration contract is exercised: quick_exit and
     * _Exit intentionally terminate the process, so they cannot be tested in
     * this same process without a child-process harness. */
    assert(cplus_at_quick_exit(c17_quick_exit_handler) == 0);
#endif

    puts("test_stdlib_c17: ok");
    return 0;
}
