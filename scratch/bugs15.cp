#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 415 - 424: non-async-signal-safe calls made from signal handlers. */

static void bug415_handler(int sig) {
  (void)sig;
  fprintf(stderr, "signal %d\n", sig);
}
// BUG 415: fprintf() from a handler can deadlock when the signal interrupts a
// thread that already holds the stderr FILE lock or is inside malloc.
// SAFE FIX: set a volatile sig_atomic_t flag and do the diagnostic printing in
// the main thread after it observes the flag.
void bug415_fprintf_from_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug415_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
}

static void bug416_handler(int sig) {
  (void)sig;
  puts("handler entered");
}
// BUG 416: puts() touches shared stdio buffers and locks; it is not
// async-signal-safe and may deadlock or corrupt stdout.
// SAFE FIX: write a fixed message with write(2) in the handler, or record a
// flag and print later from normal code.
void bug416_puts_from_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug416_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
}

static void *bug417_scratch;
static void bug417_handler(int sig) {
  (void)sig;
  bug417_scratch = malloc(256);
}
// BUG 417: malloc() from a handler re-enters the allocator. If the interrupted
// code was in malloc/free the heap lock is held by the same thread, causing a
// permanent deadlock or heap metadata corruption.
// SAFE FIX: pre-allocate a fixed buffer before installing the handler and have
// the handler only set a flag that says the buffer is ready.
void bug417_reentrant_malloc(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug417_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  (void)bug417_scratch;
}

static char *bug417_victim;
static void bug418_handler(int sig) {
  (void)sig;
  free(bug417_victim);
  bug417_victim = NULL;
}
// BUG 418: free() in a handler races with a reader in main; the reader may keep
// using the pointer after the handler freed it, or free it a second time.
// SAFE FIX: never transfer ownership from inside a handler; mark the resource
// abandoned with a sig_atomic_t flag and reclaim it in the interrupted thread.
void bug418_free_from_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug418_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  bug417_victim = (char *)malloc(32);
  (void)bug417_victim;
}

static void bug419_handler(int sig) {
  (void)sig;
  FILE *f = fopen("/tmp/bug419.log", "a");
  if (f) {
    fputc('x', f);
    fclose(f);
  }
}
// BUG 419: fopen()/fclose() and buffered I/O in a handler may lock the same
// stdio structures as interrupted code and are not async-signal-safe.
// SAFE FIX: log from ordinary code, or use a pre-opened fd and a single
// async-signal-safe write() call with no formatting.
void bug419_fopen_fclose_from_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug419_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
}

static char bug420_msg[64];
static void bug420_handler(int sig) {
  (void)sig;
  sprintf(bug420_msg, "signal=%d", sig);
}
// BUG 420: sprintf() from a handler uses locale and stdio-adjacent state that
// is not reentrant; an interrupted sprintf in main leaves inconsistent state.
// SAFE FIX: store just the signal number in a volatile sig_atomic_t and format
// the text after returning to normal control flow.
void bug420_sprintf_in_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug420_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  (void)bug420_msg;
}

static char *bug421_storage;
static void bug421_handler(int sig) {
  (void)sig;
  char *grown = (char *)realloc(bug421_storage, 128);
  if (grown)
    bug421_storage = grown;
}
// BUG 421: realloc() in a handler can free the old block while main is reading
// it, and re-enters the allocator lock if main was already inside malloc.
// SAFE FIX: defer resizing until the signal has been consumed in normal code;
// the handler should only set a one-shot flag.
void bug421_realloc_in_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug421_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  bug421_storage = (char *)malloc(32);
  (void)bug421_storage;
}

static void *bug422_scratch;
static void bug422_handler(int sig) {
  (void)sig;
  bug422_scratch = calloc(4, sizeof(uint64_t));
}
// BUG 422: calloc() is just as unsafe as malloc() in a handler: the same
// non-reentrant allocator bookkeeping is touched.
// SAFE FIX: pre-allocate and zero the storage before installing the handler;
// let the handler only flip a sig_atomic_t flag.
void bug422_calloc_in_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug422_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  (void)bug422_scratch;
}

static void bug423_handler(int sig) {
  (void)sig;
  struct tm *t = localtime(&(time_t){0});
  (void)t;
}
// BUG 423: localtime() returns a pointer to static storage and mutates shared
// timezone state; a handler calling it can corrupt an interrupted localtime()
// and hand back a buffer that changes underneath the reader.
// SAFE FIX: do not call time conversion routines in handlers; record a flag
// and call localtime_r() from normal code with caller-owned storage.
void bug423_localtime_in_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug423_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
}

static pthread_mutex_t bug424_mtx = PTHREAD_MUTEX_INITIALIZER;
static void bug424_handler(int sig) {
  (void)sig;
  pthread_mutex_lock(&bug424_mtx);
  pthread_mutex_unlock(&bug424_mtx);
}
// BUG 424: the handler takes a mutex that the interrupted thread may already
// hold; pthread_mutex_lock() is not async-signal-safe and self-deadlocks.
// SAFE FIX: use only flag writes in the handler and lock the mutex in the
// normal thread afterwards, or use a lock-free signal-safe protocol.
void bug424_mutex_from_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug424_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  pthread_mutex_lock(&bug424_mtx);
  pthread_mutex_unlock(&bug424_mtx);
}

int main(void) {
  return 0;
}
