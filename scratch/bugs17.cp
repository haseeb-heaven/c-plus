#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* 435 - 444: sigwait/alarm races, async reentrancy and torn reads. */

static volatile sig_atomic_t bug435_got;
static void bug435_handler(int sig) {
  (void)sig;
  bug435_got = 1;
}
// BUG 435: sigwait() is called for SIGUSR1 that was never blocked. The signal
// is delivered to the process handler (or kills the process) instead of being
// synchronously accepted, so sigwait() can block forever.
// SAFE FIX: block SIGUSR1 with pthread_sigmask() in every thread before calling
// sigwait(), and do not install a competing handler for that signal.
void bug435_sigwait_unblocked_signal(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug435_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  sigset_t set = {0};
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  int sig = 0;
  (void)sigwait(&set, &sig);
}

static void *bug436_worker(void *arg) {
  (void)arg;
  pause();
  return NULL;
}
// BUG 436: the signal is blocked only in the thread that calls sigwait();
// other threads inherit a different mask, so the kernel may deliver the signal
// to one of them and sigwait() misses it entirely.
// SAFE FIX: block the waited-for signal before creating any threads (so all
// threads inherit the blocked mask) or explicitly set each thread's mask.
void bug436_sigwait_mask_not_inherited(void) {
  pthread_t tid;
  pthread_create(&tid, NULL, bug436_worker, NULL);
  sigset_t set = {0};
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL);
  int sig = 0;
  (void)sigwait(&set, &sig);
  pthread_join(tid, NULL);
}

static uint64_t bug437_counter;
static void bug437_handler(int sig) {
  (void)sig;
  bug437_counter = 0x1122334455667788ULL;
}
// BUG 437: a handler writes a 64-bit object that the main thread reads without
// synchronization. On a 32-bit ABI the two halves are stored separately, so
// the reader may combine an old high word with a new low word.
// SAFE FIX: share only volatile sig_atomic_t flags with handlers; keep 64-bit
// data in normal code protected by a mutex or C11 atomics (never in a handler).
void bug437_torn_64bit_read(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug437_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  uint64_t seen = bug437_counter;
  (void)seen;
}

// BUG 438: alarm(1) is armed before pause(). If SIGALRM arrives between those
// two calls, pause() has no signal left to wait for and blocks indefinitely.
// SAFE FIX: either use sigsuspend() with the signal unblocked in the mask, or
// check a volatile sig_atomic_t flag in a loop after arming the timer.
void bug438_alarm_pause_race(void) {
  alarm(1);
  pause();
}

static volatile sig_atomic_t bug439_fired;
static void bug439_handler(int sig) {
  (void)sig;
  bug439_fired = 1;
}
// BUG 439: alarm(0) only disarms the timer; a SIGALRM already generated stays
// pending and is delivered after the "cancelled" section, surprising code that
// assumed no signal can arrive there.
// SAFE FIX: block SIGALRM, drain pending signals with sigtimedwait(), then
// unblock; keep a generation counter in a sig_atomic_t flag.
void bug439_alarm_cancel_assumption(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug439_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGALRM, &sa, NULL);
  alarm(1);
  usleep(2000);
  alarm(0);
  if (bug439_fired)
    return;
}

static char *bug440_shared;
static void bug440_handler(int sig) {
  (void)sig;
  free(bug440_shared);
  bug440_shared = NULL;
}
// BUG 440: the handler frees the buffer while main may be inside realloc() on
// that same pointer; the allocator is re-entered from a signal and its internal
// lock is not recursive.
// SAFE FIX: defer all deallocation to normal control flow; the handler only
// sets a volatile sig_atomic_t cancellation flag.
void bug440_reentrant_allocator_free(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug440_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  char *grown = (char *)realloc(bug440_shared, 128);
  if (grown)
    bug440_shared = grown;
}

static char bug441_buf[128];
static void bug441_handler(int sig) {
  (void)sig;
  snprintf(bug441_buf, sizeof bug441_buf, "signal %d", sig);
}
// BUG 441: formatting into a shared buffer from a handler races with main's
// own formatting; snprintf is not on the async-signal-safe list and the
// interrupted writer can leave the buffer half-updated.
// SAFE FIX: store the integer signal number in a sig_atomic_t and format into
// caller-owned storage later, outside the handler.
void bug441_shared_format_buffer(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug441_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  snprintf(bug441_buf, sizeof bug441_buf, "main context");
}

static void bug442_handler(int sig) {
  (void)sig;
  errno = EINTR;
}
// BUG 442: a handler that changes errno without saving and restoring it can
// overwrite the error code of the interrupted code; after the handler returns
// innocent code observes the wrong errno value.
// SAFE FIX: `int saved = errno;` at handler entry and `errno = saved;` before
// returning, or avoid errno-dependent work in the handler entirely.
void bug442_errno_not_restored(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug442_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  errno = 0;
  if (read(STDIN_FILENO, NULL, 0) < 0)
    (void)errno;
}

static void bug443_handler(int sig) {
  (void)sig;
}
// BUG 443: SA_RESTART is assumed to restart select(), but select() is never
// restarted on signal delivery; code that ignores EINTR treats -1 as failure
// or as a completed wait.
// SAFE FIX: always check select()'s return value and retry on EINTR, regardless
// of SA_RESTART, or use pselect() with a controlled signal mask.
void bug443_sa_restart_select_assumed(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug443_handler;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  fd_set rfds = {0};
  FD_ZERO(&rfds);
  struct timeval tv = {0, 0};
  (void)select(0, &rfds, NULL, NULL, &tv);
}

// BUG 444: sigwait() with an empty or uninitialized signal set waits for
// nothing: POSIX requires EINVAL or returns only for a signal that can never
// be generated, so the "wait for shutdown" thread hangs.
// SAFE FIX: call sigemptyset() and sigaddset() for every expected signal, check
// the return value of sigwait(), and block those signals in all threads.
void bug444_sigwait_empty_set(void) {
  sigset_t set = {0};
  sigemptyset(&set);
  int sig = 0;
  int rc = sigwait(&set, &sig);
  if (rc == 0)
    (void)sig;
}

int main(void) {
  return 0;
}
