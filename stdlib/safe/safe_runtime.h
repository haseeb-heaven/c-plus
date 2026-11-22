#ifndef CPLUS_SAFE_RUNTIME_H
#define CPLUS_SAFE_RUNTIME_H
/* C+ recoverable runtime-error handling (Agent 2 library surface).
 *
 * Purpose: when a program is built with recoverable sanitizers
 * (`-fsanitize-recover=all`), a detected runtime error (out-of-bounds, null,
 * misalignment, signed overflow) is routed to a handler that reports it and
 * unwinds to a guard, instead of aborting the process.
 *
 * IMPORTANT - what unwinding does and does not do
 * -----------------------------------------------
 * 1. This does NOT make the erroneous operation safe, and it MUST NOT be used
 *    to silently continue past memory corruption.  UBSan reports *before* the
 *    faulting access executes (bounds/null/alignment), and the handler leaves by
 *    longjmp before returning to it, so the bad access is not performed.  For a
 *    process that has ALREADY corrupted memory (e.g. by a raw wild write), no
 *    runtime can restore safety: use cplus_runtime_set_failstop(1) so any
 *    report terminates the process instead of unwinding.
 * 2. longjmp does NOT run C+ scope destructors of the frames it skips.  Any
 *    resource that must be released when a guarded body is abandoned has to be
 *    registered with cplus_runtime_push_cleanup(); the guard then runs those
 *    cleanups exactly once, in reverse (LIFO) order, before returning
 *    CPLUS_RUNTIME_ERROR.  Resources that are NOT registered are leaked on an
 *    unwind - that is a documented limitation, covered by tests, and it needs
 *    compiler-side cooperation (Agent 1) to close for compiler-owned cleanup.
 * 3. Guards nest; each guard owns its own jump frame and cleanup watermark.
 * 4. Guards and cleanup stacks are thread-local.
 */
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define CPLUS_RUNTIME_OK      0
#define CPLUS_RUNTIME_ERROR   1

typedef void (*cplus_runtime_handler)(const char *message);

/* Install a process/thread handler for runtime errors (report-only hooks). */
void cplus_runtime_set_handler(cplus_runtime_handler handler);

/* When enabled, every runtime-error report terminates the process (exit 70)
 * even inside a guard.  Prefer this when memory corruption is suspected. */
void cplus_runtime_set_failstop(int enabled);
int cplus_runtime_get_failstop(void);

/* Control SafeCRT violation handling (UAF and auto-bounds overflow checks).
 * Enabled (1) by default: unguarded SafeCRT violations terminate (exit 70)
 * instead of allowing execution to continue with stale/corrupted data; inside
 * cplus_runtime_guard() they unwind unless failstop is enabled.  Set to 0 in
 * tests that inspect NULL/ERANGE/EFAULT return values. */
void cplus_runtime_set_violation_trap(int enabled);
int cplus_runtime_get_violation_trap(void);
void cplus_runtime_violation(const char *api, const char *detail);

/* Report a runtime error (called by the UBSan hook and by library code). */
void cplus_runtime_report(const char *message);

/* Run `body(ctx)` under a recovery guard. Returns CPLUS_RUNTIME_OK if the body
 * completed, or CPLUS_RUNTIME_ERROR if a runtime error was detected and the
 * body was unwound. The error is always reported; it is never silent.
 * Cleanups still registered when the body returns normally are also run (and
 * counted as a leak by cplus_runtime_leaked_cleanups()). */
int cplus_runtime_guard(int (*body)(void *ctx), void *ctx);

/* Cleanup registry (thread-local LIFO) -------------------------------------
 * push: register `drop(self)` to run on unwind.  0 on success, -1 when the
 *       stack is full (CPLUS_RUNTIME_CLEANUP_MAX) or drop is NULL.
 * pop:  remove the most recent entry; run it when `execute` != 0.
 * mark/unwind_to: save the depth, later run (LIFO) everything above it.
 * unwind_cleanups: run everything registered on this thread.             */
#define CPLUS_RUNTIME_CLEANUP_MAX 64
int cplus_runtime_push_cleanup(void (*drop)(void *), void *self);
void cplus_runtime_pop_cleanup(int execute);
int cplus_runtime_cleanup_mark(void);
void cplus_runtime_cleanup_unwind_to(int mark);
void cplus_runtime_unwind_cleanups(void);
int cplus_runtime_cleanup_depth(void);
/* Number of cleanups a guard had to run because the body returned normally
 * without popping them (diagnostic; reset with cplus_runtime_reset_counters). */
unsigned long cplus_runtime_leaked_cleanups(void);
unsigned long cplus_runtime_unwound_cleanups(void);
void cplus_runtime_reset_counters(void);

#if defined(__cplusplus)
}
#endif
#endif
