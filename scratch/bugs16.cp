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
#include <unistd.h>

/* 425 - 434: signal masks, SA_RESTART, siglongjmp and stale jump buffers. */

static sigjmp_buf bug425_env;
static void bug425_handler(int sig) {
  (void)sig;
  siglongjmp(bug425_env, 1);
}
// BUG 425: sigsetjmp(env, 0) does not save the signal mask, so siglongjmp out of
// the handler leaves the delivered signal blocked forever in this thread.
// SAFE FIX: use sigsetjmp(env, 1) (savesigs != 0) so siglongjmp restores the
// pre-handler mask, or return normally from the handler instead of jumping.
void bug425_siglongjmp_mask_not_saved(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug425_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  if (sigsetjmp(bug425_env, 0) == 0) {
    pause();
  } else {
    sigset_t cur = {0};
    sigprocmask(SIG_BLOCK, NULL, &cur);
    (void)cur;
  }
}

static void bug426_handler(int sig) {
  (void)sig;
  sigset_t block;
  sigemptyset(&block);
  sigaddset(&block, SIGUSR1);
  sigprocmask(SIG_BLOCK, &block, NULL);
}
// BUG 426: the handler changes the thread signal mask and returns without
// restoring the old mask, permanently suppressing later deliveries.
// SAFE FIX: let sigaction()/sigreturn manage the mask; if the handler must
// block signals, call sigprocmask(SIG_SETMASK, &old, NULL) before returning.
void bug426_mask_not_restored(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug426_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  raise(SIGUSR1);
}

static sigjmp_buf bug427_env;
static void bug427_handler(int sig) {
  (void)sig;
  fprintf(stderr, "handling signal, about to jump\n");
  siglongjmp(bug427_env, 1);
}
// BUG 427: the handler performs buffered fprintf() before jumping out. The
// stdio lock may still be held by the interrupted code and the jump skips any
// pending flush, so the message is lost or the stream is left inconsistent.
// SAFE FIX: set a flag and return normally; let normal code log and decide how
// to unwind, or use a single async-signal-safe write() before jumping.
void bug427_fprintf_then_siglongjmp(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug427_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  if (sigsetjmp(bug427_env, 1) == 0)
    pause();
}

static volatile sig_atomic_t bug428_flag;
static void bug428_handler(int sig) {
  (void)sig;
  bug428_flag = 1;
}
// BUG 428: code installed with signal() assumes read() is automatically
// restarted (SA_RESTART). It is not portable, and a short read/EINTR is
// mistaken for a successful read of zero bytes.
// SAFE FIX: install with sigaction(), set SA_RESTART only when the semantics
// are actually wanted, and always retry read() on EINTR.
void bug428_sa_restart_assumed(void) {
  signal(SIGUSR1, bug428_handler);
  char buf[16];
  ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
  if (n < 0 && errno == EINTR)
    (void)n; // ERROR: assumed restart; real code must retry the read
}

struct bug429_task {
  jmp_buf env;
  void *heap_scratch;
};
// BUG 429: a jmp_buf lives inside a heap task object. Once the task returns and
// its storage is released, longjmp() restores a stack context whose frame no
// longer exists; the subsequent code resumes into dead memory.
// SAFE FIX: keep jmp_buf in the frame that calls setjmp() and ensure longjmp()
// happens strictly before that frame returns; free the task only after no
// jump is possible.
void bug429_jmp_buf_in_freed_task(void) {
  struct bug429_task *t = (struct bug429_task *)malloc(sizeof(*t));
  if (!t)
    return;
  if (setjmp(t->env) == 0) {
    free(t);
    return;
  }
  free(t->heap_scratch);
}

static jmp_buf bug430_outer;
static jmp_buf bug430_inner;
static void bug430_helper(void) {
  if (setjmp(bug430_inner) == 0)
    longjmp(bug430_outer, 1);
}
// BUG 430: nested longjmp. The inner jmp_buf belongs to bug430_helper(), which
// returned when the outer jump was taken; a later jump to bug430_inner resumes
// a destroyed frame.
// SAFE FIX: never keep a jmp_buf belonging to a frame that has returned; use a
// single jump target at the top-level driver and propagate errors as codes.
void bug430_nested_longjmp_stale_frame(void) {
  if (setjmp(bug430_outer) == 0) {
    bug430_helper();
  } else {
    longjmp(bug430_inner, 1);
  }
}

static jmp_buf bug431_env;
// BUG 431: longjmp() bypasses fclose()/fflush() for a FILE opened after the
// setjmp point. Buffered output is lost and the descriptor leaks on every jump.
// SAFE FIX: register cleanup that runs at the jump target, or avoid non-local
// exits while an owned resource is live; use structured return codes instead.
void bug431_longjmp_skips_fclose(void) {
  FILE *f = fopen("/tmp/bug431.log", "w");
  if (!f)
    return;
  if (setjmp(bug431_env) == 0) {
    fputs("partial", f);
    longjmp(bug431_env, 1);
  }
  fclose(f);
}

static pthread_mutex_t bug432_mtx = PTHREAD_MUTEX_INITIALIZER;
static jmp_buf bug432_env;
// BUG 432: longjmp() escapes the critical section without unlocking the
// mutex; every other thread blocks forever on a lock with no live owner.
// SAFE FIX: release the mutex before jumping, or restructure so the jump
// target performs the unlock; prefer a normal return carrying an error code.
void bug432_longjmp_leaves_mutex_locked(void) {
  pthread_mutex_lock(&bug432_mtx);
  if (setjmp(bug432_env) == 0)
    longjmp(bug432_env, 1);
  pthread_mutex_unlock(&bug432_mtx);
}

struct bug433_holder {
  jmp_buf env;
};
// BUG 433: a jmp_buf is copied by value into another object and longjmp() is
// called through the copy. jmp_buf is an opaque array type; copying it is not
// guaranteed to preserve the machine context on every ABI.
// SAFE FIX: pass the original jmp_buf directly to the function that may jump
// and never copy, assign or serialize it.
void bug433_jmp_buf_copied_by_value(void) {
  jmp_buf original;
  struct bug433_holder copy;
  memcpy(copy.env, original, sizeof copy.env);
  if (setjmp(original) == 0)
    longjmp(copy.env, 1);
}

static sigjmp_buf bug434_env;
static void bug434_handler(int sig) {
  (void)sig;
  siglongjmp(bug434_env, 1);
}
// BUG 434: siglongjmp() restores a setjmp() target whose function has already
// returned. The handler can fire long after that frame vanished, so the
// register set and stack pointer describe a dead activation.
// SAFE FIX: install the handler only for the lifetime of the frame that called
// sigsetjmp() and restore the previous disposition before that frame returns.
void bug434_siglongjmp_to_dead_frame(void) {
  struct sigaction sa = {0};
  struct sigaction old;
  sa.sa_handler = bug434_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, &old);
  if (sigsetjmp(bug434_env, 1) == 0)
    return;
  sigaction(SIGUSR1, &old, NULL);
}

int main(void) {
  return 0;
}
