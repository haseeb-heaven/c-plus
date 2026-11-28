/* Regression for issue #194: a guard frame abandoned by an outer longjmp
 * must never be used as a longjmp target, and the atomically published
 * control state (handler / failstop / trap) must observe setter writes. */
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
    return 123; /* unreachable: the trap unwinds to the live guard */
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
    /* A LIVE guard is still unwound by a violation (no fail-closed exit). */
    assert(cplus_runtime_guard(violating_body, NULL) == CPLUS_RUNTIME_ERROR);

    /* Atomically published control state: getters observe setter writes. */
    cplus_runtime_set_failstop(1);
    assert(cplus_runtime_get_failstop() == 1);
    cplus_runtime_set_failstop(0);
    assert(cplus_runtime_get_failstop() == 0);
    cplus_runtime_set_handler(counting_handler);
    cplus_runtime_report("issue194 handler probe");
    assert(g_handler_calls == 1);
    cplus_runtime_set_handler(NULL);

    /* Leave a guard by an outer longjmp; its jmp_buf is now a dead stack
     * frame, yet the runtime still exposes it as the innermost guard. */
    if (setjmp(g_outer_jb) == 0) {
        (void)cplus_runtime_guard(escaping_body, NULL);
        assert(0); /* not reached */
    }

    /* Trigger a violation in a child: the stale frame must be rejected and
     * the process must fail closed with the defined exit code 70. */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        cplus_runtime_set_handler(NULL);
        cplus_runtime_violation("issue194", "stale guard frame");
        _exit(99); /* reached only if the stale jmp_buf was (unsafely) used */
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 70);

    puts("issue194_runtime_guard_frame: ok");
    return 0;
}
