#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 405 - 414: sig_atomic_t violations and unsafe shared-state writes. */

static int bug405_stop;
static void bug405_handler(int sig) {
  (void)sig;
  bug405_stop = 1;
}
// BUG 405: handler writes a plain int flag; the main loop may cache it in a
// register forever because the object is not volatile sig_atomic_t.
// SAFE FIX: use `static volatile sig_atomic_t bug405_stop` and read that single
// object in the loop; install the handler with sigaction().
void bug405_plain_int_stop_flag(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug405_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  while (!bug405_stop)
    pause();
}

static uint64_t bug406_ticks;
static void bug406_handler(int sig) {
  (void)sig;
  bug406_ticks = 1;
}
// BUG 406: 64-bit counter written by a handler is not sig_atomic_t; on a 32-bit
// target the two 32-bit stores may tear and the reader sees a mixed value.
// SAFE FIX: use a volatile sig_atomic_t flag plus atomics or a lock obtained
// outside the handler; never share a wider scalar with a signal handler.
void bug406_wide_counter_from_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug406_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGALRM, &sa, NULL);
  alarm(1);
  (void)bug406_ticks;
}

struct bug407_status {
  int ready;
  int code;
};
static struct bug407_status bug407_st;
static void bug407_handler(int sig) {
  (void)sig;
  bug407_st.ready = 1;
  bug407_st.code = sig;
}
// BUG 407: a handler publishes a two-field aggregate one member at a time; the
// reader can observe ready==1 with an unrelated code (torn publication).
// SAFE FIX: publish a single volatile sig_atomic_t token and keep the detailed
// status in a buffer that is only touched while the signal is blocked.
void bug407_nonatomic_aggregate_publish(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug407_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  if (bug407_st.ready)
    (void)bug407_st.code;
}

static volatile sig_atomic_t bug408_count;
static void bug408_handler(int sig) {
  (void)sig;
  bug408_count++;
}
// BUG 408: even a sig_atomic_t object cannot be safely incremented in a
// handler; ++ is read-modify-write and two deliveries can lose an update.
// SAFE FIX: make the handler only set a flag and count deliveries in normal
// control flow, or use a lock-free atomic fetched in the handler only if the
// platform documents it as async-signal-safe.
void bug408_sig_atomic_increment(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug408_handler;
  sa.sa_flags = SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR2, &sa, NULL);
  raise(SIGUSR2);
  (void)bug408_count;
}

static char *bug409_shared;
static void bug409_handler(int sig) {
  (void)sig;
  free(bug409_shared);
  bug409_shared = NULL;
}
// BUG 409: the handler frees shared heap state and clears the pointer. If the
// interrupted code was inside malloc/free the allocator lock is re-entered and
// the process deadlocks; main may also free a pointer the handler just freed.
// SAFE FIX: only set a volatile sig_atomic_t "stop" flag in the handler and do
// the free() in normal control flow after the signal has been consumed.
void bug409_handler_frees_shared_state(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug409_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  bug409_shared = (char *)malloc(64);
  (void)bug409_shared;
}

static int bug410_flag;
static int *bug410_alias;
static void bug410_handler(int sig) {
  (void)sig;
  *bug410_alias = 1;
}
// BUG 410: the handler writes a non-atomic global through an alias pointer, so
// the compiler's data-flow analysis for the original object is bypassed and
// readers may still use a stale cached value.
// SAFE FIX: write the declared volatile sig_atomic_t object directly and do not
// keep non-volatile aliases to handler-owned state.
void bug410_flag_written_through_alias(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug410_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  bug410_alias = &bug410_flag;
  while (!bug410_flag)
    pause();
}

static double bug411_value;
static void bug411_handler(int sig) {
  (void)sig;
  bug411_value = 1.0;
}
// BUG 411: floating-point storage is not sig_atomic_t and a handler must not
// modify it; a reader can observe a partially written representation.
// SAFE FIX: signal only a volatile sig_atomic_t flag and compute/store the
// floating-point value after returning from the handler.
void bug411_floating_point_global(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug411_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGFPE, &sa, NULL);
  (void)bug411_value;
}

static volatile sig_atomic_t bug412_count;
static void bug412_first(int sig) {
  (void)sig;
  bug412_count += 1;
}
static void bug412_second(int sig) {
  (void)sig;
  bug412_count += 1;
}
// BUG 412: two signals can run their handlers at the same time when SA_NODEFER
// is used; the read-modify-write on the shared flag loses one delivery.
// SAFE FIX: let SA_NODEFER handlers only set distinct one-shot flags, or block
// the other signals in sa_mask so the increments cannot interleave.
void bug412_two_handlers_same_counter(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug412_first;
  sa.sa_flags = SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  sa.sa_handler = bug412_second;
  sigaction(SIGUSR2, &sa, NULL);
  (void)bug412_count;
}

static int bug413_flags[8];
static void bug413_handler(int sig) {
  (void)sig;
  bug413_flags[(unsigned)sig % 8] = 1;
}
// BUG 413: an array element of plain int is written from a handler; neither
// the element type nor the publication of the index is atomic.
// SAFE FIX: use one volatile sig_atomic_t bitmask word if it fits, or separate
// per-signal volatile sig_atomic_t flags with no shared index arithmetic.
void bug413_global_array_element(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug413_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  (void)bug413_flags[0];
}

static size_t bug414_len;
static char bug414_buf[64];
static void bug414_handler(int sig) {
  (void)sig;
  bug414_len = 32;
}
// BUG 414: a handler changes a size_t length used to bound a reader's copy;
// the reader may see a torn or stale length and read past the real data.
// SAFE FIX: keep the length immutable and let the handler only raise a
// volatile sig_atomic_t flag that the reader checks before using the length.
void bug414_handler_changes_shared_length(void) {
  struct sigaction sa = {0};
  sa.sa_handler = bug414_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  char out[64];
  memcpy(out, bug414_buf, bug414_len);
}

int main(void) {
  return 0;
}
