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
#include <time.h>

/* ============================================================================
 * 365-374: condition variables, mutexes and rwlocks used incorrectly
 * ============================================================================
 * No function below is called by main().  Each SAFE FIX comment names the
 * correct POSIX pattern that removes the race or deadlock.
 */

static pthread_mutex_t bug365_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bug365_cv = PTHREAD_COND_INITIALIZER;
static int bug365_events = 0;

static void *bug365_notifier(void *arg) {
  (void)arg;
  /* The predicate is modified without holding the mutex associated with the
     condition variable, and the waiter below uses if instead of while. */
  bug365_events++;
  pthread_cond_signal(&bug365_cv);
  return NULL;
}

// BUG 365: pthread_cond_wait() is guarded by "if" rather than a predicate
// loop, so a spurious wakeup lets the waiter proceed with a false predicate;
// the shared event count is also mutated without the condition mutex.
// SAFE FIX: hold the mutex whenever the predicate changes and use
// while (events == 0) pthread_cond_wait(&cv, &mu);
void bug365_cond_wait_no_predicate_loop(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug365_notifier, NULL);
  pthread_create(&b, NULL, bug365_notifier, NULL);
  pthread_mutex_lock(&bug365_mu);
  if (bug365_events == 0)
    pthread_cond_wait(&bug365_cv, &bug365_mu);
  printf("events=%d\n", bug365_events);
  pthread_mutex_unlock(&bug365_mu);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static pthread_mutex_t bug366_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bug366_cv = PTHREAD_COND_INITIALIZER;
static int bug366_ready = 0;

static void *bug366_signal_first(void *arg) {
  (void)arg;
  /* No mutex held: a wakeup can be delivered between the waiter's predicate
     test and its call to pthread_cond_wait, and then be lost forever. */
  pthread_cond_signal(&bug366_cv);
  pthread_mutex_lock(&bug366_mu);
  bug366_ready = 1;
  pthread_mutex_unlock(&bug366_mu);
  return NULL;
}

// BUG 366: pthread_cond_signal() is called outside the mutex that protects
// the predicate, creating the classic lost-wakeup window.
// SAFE FIX: change the predicate under the mutex and signal while still
// holding it (or immediately after unlocking with a re-check).
void bug366_signal_without_mutex(void) {
  pthread_t t;
  pthread_create(&t, NULL, bug366_signal_first, NULL);
  pthread_mutex_lock(&bug366_mu);
  while (bug366_ready == 0)
    pthread_cond_wait(&bug366_cv, &bug366_mu);
  pthread_mutex_unlock(&bug366_mu);
  pthread_join(t, NULL);
}

static pthread_mutex_t bug367_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bug367_cv = PTHREAD_COND_INITIALIZER;
static int bug367_pred = 0;

static void *bug367_notifier(void *arg) {
  (void)arg;
  pthread_mutex_lock(&bug367_mu);
  /* Waking before the predicate is set lets the waiter wake, observe the old
     value, and go back to sleep with nobody left to signal it. */
  pthread_cond_signal(&bug367_cv);
  bug367_pred = 1;
  pthread_mutex_unlock(&bug367_mu);
  return NULL;
}

// BUG 367: condition signalled before updating the predicate it describes.
// SAFE FIX: update the predicate first, then call pthread_cond_signal/broadcast.
void bug367_signal_before_predicate(void) {
  pthread_t t;
  pthread_create(&t, NULL, bug367_notifier, NULL);
  pthread_mutex_lock(&bug367_mu);
  while (bug367_pred == 0)
    pthread_cond_wait(&bug367_cv, &bug367_mu);
  pthread_mutex_unlock(&bug367_mu);
  pthread_join(t, NULL);
}

static pthread_mutex_t bug368_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bug368_cv = PTHREAD_COND_INITIALIZER;
static int bug368_jobs = 0;

static void *bug368_waiter(void *arg) {
  (void)arg;
  pthread_mutex_lock(&bug368_mu);
  while (bug368_jobs == 0)
    pthread_cond_wait(&bug368_cv, &bug368_mu);
  bug368_jobs--;
  pthread_mutex_unlock(&bug368_mu);
  return NULL;
}

static void *bug368_producer(void *arg) {
  (void)arg;
  pthread_mutex_lock(&bug368_mu);
  bug368_jobs = 2;
  pthread_cond_signal(&bug368_cv); /* only one of the two waiters wakes */
  pthread_mutex_unlock(&bug368_mu);
  return NULL;
}

// BUG 368: a single pthread_cond_signal() is used when two threads wait on
// distinct predicates guarded by the same condition variable.
// SAFE FIX: use pthread_cond_broadcast(), or give each waiter its own
// condition variable/predicate and signal the right one.
void bug368_signal_instead_of_broadcast(void) {
  pthread_t w1, w2, p;
  pthread_create(&w1, NULL, bug368_waiter, NULL);
  pthread_create(&w2, NULL, bug368_waiter, NULL);
  pthread_create(&p, NULL, bug368_producer, NULL);
  pthread_join(w1, NULL);
  pthread_join(w2, NULL);
  pthread_join(p, NULL);
}

static pthread_mutex_t bug369_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t bug369_owner;

static void *bug369_other_thread(void *arg) {
  (void)arg;
  /* POSIX: unlocking a mutex from a thread other than the owner is
     undefined behaviour for a normal mutex. */
  pthread_mutex_unlock(&bug369_mu);
  return NULL;
}

// BUG 369: a mutex is locked by one thread and unlocked by a different one.
// SAFE FIX: unlock in the owning thread, or use a robust/shared mutex whose
// semantics explicitly allow ownership transfer (and re-initialise carefully).
void bug369_unlock_wrong_thread(void) {
  pthread_t t;
  pthread_mutex_lock(&bug369_mu);
  bug369_owner = pthread_self();
  pthread_create(&t, NULL, bug369_other_thread, NULL);
  pthread_join(t, NULL);
  if (pthread_equal(bug369_owner, pthread_self()))
    pthread_mutex_unlock(&bug369_mu);
}

static pthread_mutex_t bug370_mu = PTHREAD_MUTEX_INITIALIZER;
static int bug370_data = 0;

// BUG 370: pthread_mutex_unlock() is called twice for a single lock.  The
// second unlock corrupts the mutex state and can let another thread in.
// SAFE FIX: pair every lock with exactly one unlock, e.g. with a single-exit
// path or a scoped cleanup helper.
void bug370_double_unlock(void) {
  pthread_mutex_lock(&bug370_mu);
  bug370_data++;
  pthread_mutex_unlock(&bug370_mu);
  pthread_mutex_unlock(&bug370_mu);
  printf("data=%d\n", bug370_data);
}

static pthread_rwlock_t bug371_rw = PTHREAD_RWLOCK_INITIALIZER;
static int bug371_cache = 0;

// BUG 371: an intended rwlock downgrade is implemented by releasing the read
// lock and then acquiring the write lock.  Between the two calls another
// writer can change the data, so the "downgraded" thread works from a stale
// snapshot, and under writer-preference implementations it can deadlock.
// SAFE FIX: use an implementation-supported downgrade where available, or
// hold the write lock and explicitly convert in one critical section.
void bug371_rwlock_bad_downgrade(void) {
  pthread_rwlock_rdlock(&bug371_rw);
  int snapshot = bug371_cache;
  pthread_rwlock_unlock(&bug371_rw);
  pthread_rwlock_wrlock(&bug371_rw);
  bug371_cache = snapshot + 1;
  pthread_rwlock_unlock(&bug371_rw);
}

static pthread_rwlock_t bug372_rw = PTHREAD_RWLOCK_INITIALIZER;
static void bug372_needs_write(void) {
  pthread_rwlock_wrlock(&bug372_rw);
  pthread_rwlock_unlock(&bug372_rw);
}

// BUG 372: a thread holding a read lock calls a helper that takes the same
// rwlock for writing.  Read locks are not upgradeable; this self-deadlocks on
// writer-preferring implementations.
// SAFE FIX: drop the read lock before acquiring the write lock, or document
// and use a dedicated upgrade primitive.
void bug372_rdlock_upgrade_self_deadlock(void) {
  pthread_rwlock_rdlock(&bug372_rw);
  bug372_needs_write();
  pthread_rwlock_unlock(&bug372_rw);
}

static pthread_cond_t bug373_cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t bug373_mu = PTHREAD_MUTEX_INITIALIZER;
static int bug373_done = 0;

static void *bug373_timed_waiter(void *arg) {
  (void)arg;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += 1;
  pthread_mutex_lock(&bug373_mu);
  while (bug373_done == 0)
    pthread_cond_timedwait(&bug373_cv, &bug373_mu, &ts);
  pthread_mutex_unlock(&bug373_mu);
  return NULL;
}

// BUG 373: the timespec is generated from CLOCK_REALTIME but the condition
// variable's clock attribute is CLOCK_MONOTONIC (or vice versa), so the
// deadline is interpreted in a different time base and fires immediately or
// never.
// SAFE FIX: set the condattr clock with pthread_condattr_setclock() and use
// that same clock in clock_gettime() for the deadline.
void bug373_cond_clock_mismatch(void) {
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
#if defined(__linux__)
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
  pthread_cond_destroy(&bug373_cv);
  pthread_cond_init(&bug373_cv, &attr);
  pthread_condattr_destroy(&attr);
  pthread_t t;
  pthread_create(&t, NULL, bug373_timed_waiter, NULL);
  pthread_join(t, NULL);
}

static pthread_mutex_t bug374_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bug374_cv = PTHREAD_COND_INITIALIZER;

static void *bug374_waker(void *arg) {
  (void)arg;
  /* A bare signal with no state change: if the waiter has not reached
     pthread_cond_wait yet, the notification is lost with no predicate to
     re-check. */
  pthread_cond_signal(&bug374_cv);
  return NULL;
}

// BUG 374: a condition variable is used as a one-shot event notification
// without any persistent predicate/flag; a signal that arrives before the
// wait is lost permanently.
// SAFE FIX: protect a boolean "event happened" flag with the mutex, set it
// before signalling, and wait in a predicate loop.
void bug374_signal_lost_no_predicate(void) {
  pthread_t t;
  pthread_create(&t, NULL, bug374_waker, NULL);
  pthread_mutex_lock(&bug374_mu);
  pthread_cond_wait(&bug374_cv, &bug374_mu);
  pthread_mutex_unlock(&bug374_mu);
  pthread_join(t, NULL);
}

int main(void) { return 0; }
