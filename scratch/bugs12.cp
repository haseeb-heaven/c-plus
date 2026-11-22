#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* ============================================================================
 * 385-394: seqlocks, futexes and atomic_flag gates
 * ============================================================================
 * No function here is called by main().  The futex helpers are compiled only
 * on Linux; portable fallbacks keep this catalog buildable everywhere.
 */

static int bug385_seq = 0;
static int bug385_x = 0;
static int bug385_y = 0;

static void *bug385_writer(void *arg) {
  (void)arg;
  /* The sequence counter is an ordinary int and there is no release barrier
     between the data stores and the "even" publication store. */
  bug385_seq++;
  bug385_x++;
  bug385_y++;
  bug385_seq++;
  return NULL;
}

// BUG 385: a seqlock uses a non-atomic sequence counter and no acquire fence
// in the reader, so a reader can observe a matching even sequence while the
// two data words come from different generations (a torn snapshot).
// SAFE FIX: make the sequence atomic; writer stores odd/even with release,
// reader loads with acquire and places an atomic_thread_fence(acquire)
// between the first sequence load and the data loads.
void bug385_seqlock_reader_no_acquire(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug385_writer, NULL);
  pthread_create(&b, NULL, bug385_writer, NULL);
  int s1, x, y, s2;
  do {
    s1 = bug385_seq; /* plain load: no atomicity, no acquire barrier */
    x = bug385_x;
    y = bug385_y;
    s2 = bug385_seq;
  } while (s1 != s2 || (s1 & 1));
  printf("snapshot=%d,%d\n", x, y);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static atomic_int bug386_seq = 0;
static int bug386_data = 0;

static void *bug386_writer(void *arg) {
  (void)arg;
  atomic_store_explicit(&bug386_seq, 1, memory_order_relaxed);
  bug386_data++;
  /* No release fence here: the reader may match the even sequence yet still
     see the old value of data. */
  atomic_store_explicit(&bug386_seq, 2, memory_order_relaxed);
  return NULL;
}

// BUG 386: a seqlock writer omits the release barrier before publishing the
// even sequence, so the data update is not ordered ahead of the sequence.
// SAFE FIX: use memory_order_release on the final sequence store (or an
// explicit atomic_thread_fence(memory_order_release) before it).
void bug386_seqlock_writer_no_release(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug386_writer, NULL);
  pthread_create(&b, NULL, bug386_writer, NULL);
  int s = atomic_load_explicit(&bug386_seq, memory_order_acquire);
  int d = bug386_data;
  printf("seq=%d data=%d\n", s, d);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static atomic_int bug387_seq = 0;
static int bug387_a = 0;
static int bug387_b = 0;

static void *bug387_writer(void *arg) {
  (void)arg;
  atomic_fetch_add_explicit(&bug387_seq, 1, memory_order_relaxed);
  bug387_a++;
  bug387_b++;
  atomic_fetch_add_explicit(&bug387_seq, 1, memory_order_relaxed);
  return NULL;
}

// BUG 387: the seqlock reader detects a torn read and retries, but keeps the
// values from the first attempt instead of reloading them inside the loop.
// SAFE FIX: re-read all protected fields on every retry and use/commit only
// the values from the attempt whose start and end sequence numbers match.
void bug387_seqlock_retry_stale_data(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug387_writer, NULL);
  pthread_create(&b, NULL, bug387_writer, NULL);
  unsigned s1 = (unsigned)atomic_load_explicit(&bug387_seq, memory_order_acquire);
  int first_a = bug387_a;
  int first_b = bug387_b;
  unsigned s2 = (unsigned)atomic_load_explicit(&bug387_seq, memory_order_acquire);
  while (s1 != s2 || (s1 & 1U)) {
    /* BUG: first_a/first_b are never refreshed, so the committed snapshot is
       from the aborted attempt. */
    s1 = (unsigned)atomic_load_explicit(&bug387_seq, memory_order_acquire);
    s2 = (unsigned)atomic_load_explicit(&bug387_seq, memory_order_acquire);
  }
  printf("stale=%d,%d\n", first_a, first_b);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static atomic_int bug388_seq = 0;
static int bug388_value = 0;

static void *bug388_writer(void *arg) {
  (void)arg;
  /* The sequence is never set to odd before the update starts.  A reader
     that samples here sees an even sequence and assumes a stable object. */
  bug388_value++;
  atomic_fetch_add_explicit(&bug388_seq, 2, memory_order_release);
  return NULL;
}

// BUG 388: the seqlock writer skips the odd-phase sequence increment, so
// readers can consume data while it is half-written.
// SAFE FIX: seq++ (odd) before writing, then seq++ (even) after the writes.
void bug388_seqlock_missing_odd_phase(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug388_writer, NULL);
  pthread_create(&b, NULL, bug388_writer, NULL);
  int s = atomic_load_explicit(&bug388_seq, memory_order_acquire);
  int v = bug388_value;
  printf("seq=%d value=%d\n", s, v);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

#if defined(__linux__)
static int bug389_word = 0;

static long bug389_wait(int *word, int expected) {
  return syscall(SYS_futex, word, FUTEX_WAIT, expected, NULL, NULL, 0);
}

static long bug389_wake(int *word) {
  return syscall(SYS_futex, word, FUTEX_WAKE, 1, NULL, NULL, 0);
}
#endif

static void *bug389_waker(void *arg) {
  (void)arg;
#if defined(__linux__)
  bug389_word = 1;
  (void)bug389_wake(&bug389_word);
#endif
  return NULL;
}

// BUG 389: futex FUTEX_WAIT is issued without first re-checking the word
// value.  If the waker already incremented the word and called FUTEX_WAKE,
// the wait sleeps forever because futex wakeups are not queued.
// SAFE FIX: re-read the word immediately before FUTEX_WAIT and only sleep if
// it still equals the observed value (the standard futex wait loop).
void bug389_futex_lost_wakeup(void) {
  pthread_t t;
  pthread_create(&t, NULL, bug389_waker, NULL);
#if defined(__linux__)
  int observed = bug389_word; /* deliberately re-read without a barrier */
  (void)bug389_wait(&bug389_word, observed);
#endif
  pthread_join(t, NULL);
}

#if defined(__linux__)
static int bug390_word = 0;

static long bug390_wait(int *word, int expected) {
  return syscall(SYS_futex, word, FUTEX_WAIT, expected, NULL, NULL, 0);
}

static long bug390_wake(int *word) {
  return syscall(SYS_futex, word, FUTEX_WAKE, 1, NULL, NULL, 0);
}
#endif

static void *bug390_waker(void *arg) {
  (void)arg;
#if defined(__linux__)
  /* The guard word is never cleared, so a woken waiter re-checks it, sees
     "busy", and goes back to sleep with no further wakeup coming. */
  (void)bug390_wake(&bug390_word);
#endif
  return NULL;
}

// BUG 390: FUTEX_WAKE is used without resetting the guard value that the
// waiter checks, so the wake is consumed and the waiter blocks again.
// SAFE FIX: set the word to the "available" value (with release ordering)
// before calling FUTEX_WAKE.
void bug390_futex_guard_not_cleared(void) {
  pthread_t t;
  pthread_create(&t, NULL, bug390_waker, NULL);
#if defined(__linux__)
  int expected = 1;
  (void)bug390_wait(&bug390_word, expected);
#endif
  pthread_join(t, NULL);
}

#if defined(__linux__)
static int bug391_shared_word = 0;

static long bug391_wait(int *word, int expected) {
  return syscall(SYS_futex, word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, expected,
                 NULL, NULL, 0);
}

static long bug391_wake(int *word) {
  return syscall(SYS_futex, word, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL,
                 NULL, 0);
}
#endif

// BUG 391: FUTEX_PRIVATE_FLAG is used on a futex word living in shared
// memory.  The flag tells the kernel the word is process-private, so a waker
// in another process cannot wake the waiter.
// SAFE FIX: use plain FUTEX_WAIT/FUTEX_WAKE for cross-process shared futexes,
// or keep the private flag and guarantee all waiters/wakers share an address
// space.
void bug391_futex_private_on_shared_word(void) {
#if defined(__linux__)
  int expected = bug391_shared_word;
  (void)bug391_wait(&bug391_shared_word, expected);
  (void)bug391_wake(&bug391_shared_word);
#endif
}

static atomic_flag bug392_flag; /* automatic/static zero init is not a clear */

// BUG 392: an atomic_flag with automatic storage duration is read as if it
// had been initialised with ATOMIC_FLAG_INIT; C only guarantees that for
// static-duration flags, so the initial value is unspecified.
// SAFE FIX: use ATOMIC_FLAG_INIT at declaration or call atomic_flag_clear()
// before the first test_and_set.
void bug392_uninitialised_atomic_flag(void) {
  atomic_flag local_flag;
  while (atomic_flag_test_and_set_explicit(&local_flag, memory_order_acquire)) {
    sched_yield();
  }
  atomic_flag_clear_explicit(&local_flag, memory_order_release);
}

static atomic_flag bug393_flag = ATOMIC_FLAG_INIT;

static void *bug393_reinitialiser(void *arg) {
  (void)arg;
  /* Re-initialising an in-use flag destroys the owner information; another
     thread can now acquire what was already held. */
  atomic_flag_clear(&bug393_flag);
  return NULL;
}

// BUG 393: an atomic_flag is explicitly re-initialised/cleared while another
// thread believes it still owns the corresponding gate.
// SAFE FIX: never reset a flag that may be held; use a proper state machine
// with atomic counters or acquire/release stores on an atomic_int.
void bug393_flag_reset_while_held(void) {
  pthread_t a, b;
  if (!atomic_flag_test_and_set(&bug393_flag)) {
    pthread_create(&a, NULL, bug393_reinitialiser, NULL);
    pthread_create(&b, NULL, bug393_reinitialiser, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
  }
  atomic_flag_clear(&bug393_flag);
}

static atomic_flag bug394_gate = ATOMIC_FLAG_INIT;
static int bug394_critical = 0;

static void *bug394_contender(void *arg) {
  (void)arg;
  /* Both threads call clear(), so both can pass a test that was supposed to
     be a one-time ownership check. */
  atomic_flag_clear_explicit(&bug394_gate, memory_order_relaxed);
  if (!atomic_flag_test_and_set_explicit(&bug394_gate, memory_order_acquire))
    bug394_critical++;
  return NULL;
}

// BUG 394: two threads clear the same atomic_flag gate before testing it,
// so the "only one entrant" property is lost and both enter the section.
// SAFE FIX: only the current owner clears the flag, after it has finished
// using the protected data; contenders must only call test_and_set.
void bug394_multiple_flag_clearers(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug394_contender, NULL);
  pthread_create(&b, NULL, bug394_contender, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  printf("critical=%d\n", bug394_critical);
}

int main(void) { return 0; }
