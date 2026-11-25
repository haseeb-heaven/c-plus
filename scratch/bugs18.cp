#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* 445 - 454: reentrant cleanup, stale jumps and signal-info mistakes. */

struct bug445_object {
  int id;
  char payload[16];
};
static struct bug445_object *bug445_obj;
static void bug445_handler(int sig) {
  (void)sig;
  free(bug445_obj);
  bug445_obj = NULL;
}
// BUG 445: a handler frees a shared object while main still holds the only
// reference. The next dereference is a use-after-free and the handler can also
// corrupt the allocator if it interrupted a heap operation.
// SAFE FIX: leave ownership in normal code; the handler sets a
// volatile sig_atomic_t flag and main frees the object at a safe point.
void bug445_handler_frees_live_object(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug445_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  bug445_obj = (struct bug445_object *)malloc(sizeof(*bug445_obj));
  if (bug445_obj)
    (void)bug445_obj->id;
}

static char *bug446_shared_ptr;
static void bug446_handler(int sig) {
  (void)sig;
  free(bug446_shared_ptr);
  bug446_shared_ptr = NULL;
}
// BUG 446: main and the signal handler both free the same pointer. The
// handler runs asynchronously, so main's NULL check can pass before the
// handler frees it, producing a double free and heap corruption.
// SAFE FIX: no allocation ownership in handlers; use a flag and let a single
// owner release the pointer after the flag is observed.
void bug446_handler_double_free(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug446_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  bug446_shared_ptr = (char *)malloc(48);
  free(bug446_shared_ptr);
  bug446_shared_ptr = NULL;
}

static char *bug447_storage;
static void bug447_handler(int sig) {
  (void)sig;
  memset(bug447_storage, 0, 32);
  bug447_storage[0] = (char)sig;
}
// BUG 447: the handler writes through a pointer that main may have reallocated
// (freeing the old block). The write lands in freed memory or in a block whose
// size the handler does not know.
// SAFE FIX: keep the pointer immutable during handler installation and
// communicate only through a sig_atomic_t flag; never dereference a heap
// pointer whose lifetime is managed by interrupted code.
void bug447_handler_stale_buffer(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug447_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  char *fresh = (char *)realloc(bug447_storage, 16);
  if (fresh)
    bug447_storage = fresh;
}

static sigjmp_buf bug448_env;
static void bug448_handler(int sig) {
  (void)sig;
  siglongjmp(bug448_env, 1);
}
// BUG 448: sigsetjmp(..., 0) then siglongjmp from the handler keeps SIGUSR1
// blocked in the resumed thread. The "recover and continue" path silently
// loses all future signals of that type.
// SAFE FIX: call sigsetjmp(bug448_env, 1) so the saved context includes the
// signal mask, or unblock the signal explicitly at the jump target.
void bug448_mask_lost_across_siglongjmp(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug448_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  if (sigsetjmp(bug448_env, 0) == 0)
    pause();
}

static int bug449_items[64];
static void bug449_handler(int sig) {
  (void)sig;
  qsort(bug449_items, 64, sizeof(int),
        (int (*)(const void *, const void *))NULL);
}
// BUG 449: qsort() from a handler is not async-signal-safe; it also invokes a
// comparator and may touch allocator or locale state that interrupted code
// was in the middle of using.
// SAFE FIX: set a flag in the handler and sort in ordinary control flow with a
// real comparator; never call qsort()/bsearch() from a signal handler.
void bug449_qsort_in_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug449_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
}

static jmp_buf bug450_outer_env;
static jmp_buf bug450_inner_env;
static void bug450_callee(void) {
  if (setjmp(bug450_inner_env) == 0)
    longjmp(bug450_outer_env, 1);
}
// BUG 450: the inner jump target is captured in bug450_callee(), which returns
// as soon as the outer jump is taken. A nested longjmp back to that buffer
// resumes a frame that no longer exists, corrupting the stack.
// SAFE FIX: never longjmp to a buffer whose creating function has returned;
// model retries with explicit return codes and a single top-level jump.
void bug450_nested_longjmp_to_returned_frame(void) {
  if (setjmp(bug450_outer_env) == 0) {
    bug450_callee();
  } else {
    longjmp(bug450_inner_env, 1);
  }
}

static volatile sig_atomic_t bug451_depth;
static void bug451_handler(int sig) {
  (void)sig;
  bug451_depth++;
  raise(SIGUSR1);
}
// BUG 451: SA_NODEFER allows the same signal to interrupt its own handler;
// this handler raises the signal again and recurses until the stack overflows.
// SAFE FIX: do not self-raise, block the signal in sa_mask, and keep handler
// bodies bounded and non-recursive.
void bug451_nodefer_recursive_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug451_handler;
  sa.sa_flags = SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
}

static void bug452_handler(int sig, siginfo_t *info, void *ucontext) {
  (void)sig;
  (void)ucontext;
  int v = info->si_value.sival_int;
  (void)v;
}
// BUG 452: an SA_SIGINFO handler reads si_value without checking si_code. For
// signals sent by kill()/raise() the si_value union member is not the one that
// was written, so the value is meaningless (and the union read is unspecified).
// SAFE FIX: switch on info->si_code first and only read si_value when the code
// is SI_QUEUE or SI_TIMER, which guarantee that member was populated.
void bug452_siginfo_union_assumed(void) {
  struct sigaction sa = {0};
  sa.sa_sigaction = bug452_handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  raise(SIGUSR1);
}

static void bug453_handler(int sig) {
  (void)sig;
  alarm(1);
}
// BUG 453: a SIGALRM handler rearms a one-second alarm on every delivery,
// including the alarm that was already pending or late, so timer generations
// overlap and the "once per second" contract is violated.
// SAFE FIX: use setitimer()/timer_settime() with an explicit interval and a
// generation flag, or rearm only after draining pending signals.
void bug453_handler_rearms_alarm(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug453_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGALRM, &sa, NULL);
  alarm(1);
}

static volatile sig_atomic_t bug454_seen;
static void bug454_handler(int sig) {
  (void)sig;
  bug454_seen = 1;
}
// BUG 454: code assumes signal() installs a persistent handler and that the
// disposition survives repeated deliveries. On System V semantics signal()
// resets to SIG_DFL after the first delivery, so the next signal kills the
// process (and the flag is never set again).
// SAFE FIX: install with sigaction() (which has reliable, persistent semantics)
// or reinstall the handler at every entry if signal() is unavoidable.
void bug454_signal_one_shot_assumption(void) {
  signal(SIGUSR1, bug454_handler);
  raise(SIGUSR1);
  raise(SIGUSR1);
  (void)bug454_seen;
}

int main(void) {
  return 0;
}
