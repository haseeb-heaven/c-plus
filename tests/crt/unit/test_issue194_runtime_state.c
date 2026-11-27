/* Regression for issue #194: the runtime handler / failstop / trap state is
 * published atomically (getters observe setter writes, handler is loaded once),
 * and a guard frame abandoned by an outer longjmp is never used as a longjmp
 * target - the violation fails closed with exit code 70 instead. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "safe_runtime.h"

static jmp_buf g_outer_jb;
static int g_handler_calls;

static int violating_body(void *ctx) {
    (void)ctx;
    cplus_runtime_violation("issue194", "deliberate violation");
    return 123; /* unreachable: the live guard unwinds first */
}

static int escaping_body(void *ctx) {
    (void)ctx;
    longjmp(g_outer_jb, 1); /* bypass cplus_runtime_guard's restore */
    return 0;
}

static void counting_handler(const char *message) {
    (void)message;
    ++g_handler_calls;
}

int main(void) {
    pid_t pid;
    int status = 0;

    cplus_runtime_set_violation_trap(1);
    cplus_runtime_set_failstop(0);
    assert(cplus_runtime_guard(violating_body, NULL) == CPLUS_RUNTIME_ERROR);

    cplus_runtime_set_failstop(1);
    assert(cplus_runtime_get_failstop() == 1);
    cplus_runtime_set_failstop(0);
    assert(cplus_runtime_get_failstop() == 0);
    cplus_runtime_set_handler(counting_handler);
    cplus_runtime_report("issue194 handler probe");
    assert(g_handler_calls == 1);
    cplus_runtime_set_handler(NULL);

    if (setjmp(g_outer_jb) == 0) {
        (void)cplus_runtime_guard(escaping_body, NULL);
        assert(0); /* not reached */
    }

    /* In a child, trigger a violation with g_top still pointing at the dead
     * guard frame: the stale jmp_buf must be rejected, not jumped into. */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        cplus_runtime_violation("issue194", "stale guard frame");
        _exit(99); /* reached only if the stale jmp_buf was (unsafely) used */
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 70);

    puts("issue194_runtime_state: ok");
    return 0;
}
