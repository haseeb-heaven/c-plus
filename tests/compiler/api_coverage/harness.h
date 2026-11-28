/*===-- harness.h - API coverage scenario harness ---------------*- C -*-===*
 *
 * Shared declarations for the generated API-coverage scenario suite.
 * This file is hand-written; generated_scenarios.c is produced by run_suite.py.
 *
 *===----------------------------------------------------------------------===*/
#ifndef CPLUS_API_COVERAGE_HARNESS_H
#define CPLUS_API_COVERAGE_HARNESS_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int (*scenario_fn)(void);

typedef struct {
  const char *name;  /* unique scenario name */
  const char *api;   /* exported API under test */
  scenario_fn fn;
} scenario_t;

extern const scenario_t g_scenarios[];
extern const size_t g_scenario_count;

/* Detail of the last failure, filled by CHECK / helpers. */
extern char g_detail[512];

/* Fail the current scenario with a formatted reason. */
#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      snprintf(g_detail, sizeof(g_detail), __VA_ARGS__);                       \
      return 0;                                                                \
    }                                                                          \
  } while (0)

/* -------------------------------------------------------------------------
 * Process / I/O helpers.  All helpers return 1 on PASS, 0 on FAIL and set
 * g_detail on failure.
 * ------------------------------------------------------------------------- */

/* Run fn() in a forked child.  The child must terminate with exactly
 * expect_status (WEXITSTATUS, or 128 + WTERMSIG for a signal) and its stderr
 * must contain expect_substr.  Hard timeout: 5 seconds. */
int h_fork_trap(void (*fn)(void), int expect_status, const char *expect_substr);

/* Same, but fd 0 is redirected from `input` for the child and restored in the
 * parent after the child is reaped.  Used by scan() diagnostic scenarios. */
int h_fork_trap_stdin(void (*fn)(void), int expect_status,
                      const char *expect_substr, const char *input);

/* Run fn() in-process with cplus_fatal_hook installed.  The hook captures the
 * fatal code/kind and longjmps back here before _Exit, so the fatal path is
 * observed by gcov in the parent process.  Asserts exact code, kind and
 * diagnostic substring. */
int h_hook_trap(void (*fn)(void), int expect_code, const char *expect_kind,
                const char *expect_substr);

/* Exact-value helpers. */
int h_eq_i64(int64_t got, int64_t want, const char *what);
int h_eq_u64(uint64_t got, uint64_t want, const char *what);
int h_eq_sz(size_t got, size_t want, const char *what);
int h_eq_str(const char *got, const char *want, const char *what);
int h_has_substr(const char *haystack, const char *needle, const char *what);

/* stdout capture (in-process, via dup2 on fd 1). */
void h_capture_begin(void);
const char *h_capture_end(void);

/* stdin injection (in-process, via dup2 on fd 0). */
void h_stdin_from(const char *text);
void h_stdin_restore(void);

/* Exception round-trip: enter a frame, raise, catch via longjmp. */
int h_exception_roundtrip(int code, const char *message, const char *file,
                          int line, int *out_code, const char **out_message,
                          const char **out_file, int *out_line);

/* Drop callback counter used by cplus_scope_cleanup scenarios. */
extern volatile int g_drop_calls;
void h_drop_counter_reset(void);
void h_drop_callback(void *self);

/* Called once by main before any scenario runs. */
void harness_init(void);

/* Convenient exact signal-based status for abort() scenarios. */
#define H_STATUS_SIGABRT (128 + SIGABRT)

#endif /* CPLUS_API_COVERAGE_HARNESS_H */
