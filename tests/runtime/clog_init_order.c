/* Deterministic regression test for issue #246.
 *
 * CLOG_MUTEX_INIT is intercepted so the test can observe the one invariant
 * the bug violates: clog_is_initialized must still be false at the instant
 * the mutex is initialized. Publishing it earlier (the old compare-exchange)
 * lets another thread lock a half-initialized CRITICAL_SECTION on Windows,
 * or clear the POSIX flag in the middle of a critical section.
 *
 * No sleeps, no scheduling assumptions and no timing window: the check runs
 * on the initializing thread itself.
 */
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>

static atomic_bool premature_publish;

#ifdef _WIN32
#define CLOG_TEST_MUTEX_INIT(m) InitializeCriticalSection(m)
#else
#define CLOG_TEST_MUTEX_INIT(m) atomic_flag_clear(m)
#endif

#define CLOG_MUTEX_INIT(m)                                                     \
  do {                                                                         \
    if (atomic_load(&clog_is_initialized))                                      \
      atomic_store(&premature_publish, true);                                   \
    CLOG_TEST_MUTEX_INIT(m);                                                    \
  } while (0)

#include "cplus/clog.h"

int main(void) {
  atomic_store(&premature_publish, false);

  INFO("clog init order regression");
  fflush(NULL);

  if (atomic_load(&premature_publish)) {
    fprintf(stderr,
            "clog_init_order: FAIL readiness published before mutex init\n");
    return 1;
  }
  if (!atomic_load(&clog_is_initialized)) {
    fprintf(stderr,
            "clog_init_order: FAIL readiness not published after init\n");
    return 1;
  }

  printf("clog_init_order: ok\n");
  return 0;
}
