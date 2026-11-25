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
#include <unistd.h>
#if defined(__linux__)
#include <semaphore.h>
#endif

/* ============================================================================
 * 395-404: miscellaneous POSIX thread synchronisation failures
 * ============================================================================
 * None of these functions is invoked from main().  Linux-only primitives are
 * guarded so the catalog still compiles and builds on other POSIX systems.
 */

static atomic_flag bug395_spin = ATOMIC_FLAG_INIT;

static void *bug395_spinner(void *arg) {
  (void)arg;
  /* A tight test_and_set loop with no yield/backoff can starve the owner of
     the flag on a single-core machine or with priority scheduling. */
  while (atomic_flag_test_and_set_explicit(&bug395_spin, memory_order_acquire)) {
  }
  atomic_flag_clear_explicit(&bug395_spin, memory_order_release);
  return NULL;
}

// BUG 395: spinlock acquisition has no backoff or sched_yield, so a high-
// priority spinner can starve the lock owner forever on a single CPU.
// SAFE FIX: add sched_yield()/exponential backoff in the spin loop, or use a
// pthread_mutex that parks the waiting thread in the kernel.
void bug395_spinlock_no_backoff(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug395_spinner, NULL);
  pthread_create(&b, NULL, bug395_spinner, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static int bug396_done = 0;

static void *bug396_setter(void *arg) {
  (void)arg;
  /* Plain, non-volatile, non-atomic write from two threads at once. */
  bug396_done = 1;
  return NULL;
}

// BUG 396: a busy-wait loop tests a non-atomic, non-volatile flag.  Besides
// the data race, the compiler may hoist the load out of the loop and turn it
// into an infinite loop that never observes the update.
// SAFE FIX: use _Atomic int with an acquire load (and a yield/park in the
// loop), or wait on a condition variable / semaphore.
void bug396_non_atomic_busy_wait(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug396_setter, NULL);
  pthread_create(&b, NULL, bug396_setter, NULL);
  while (!bug396_done) {
  }
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static pthread_mutex_t bug397_mu = PTHREAD_MUTEX_INITIALIZER;
static int bug397_state = 0;

static void bug397_prepare(void) {
  /* BUG: no pthread_mutex_lock(&bug397_mu) here.  A fork from another thread
     can therefore snapshot state while this mutex is held and locked. */
  (void)bug397_state;
}

static void bug397_parent(void) { /* no counterpart unlock needed when no lock
                                     is taken in prepare() */ }

static void bug397_child(void) {
  /* Child inherits the stale state; trying to use it can deadlock. */
  printf("state=%d\n", bug397_state);
}

// BUG 397: a pthread_atfork() prepare handler does not acquire the
// application mutex, so a child forked from a multithreaded process can
// observe a half-updated state (and deadlock if it later takes that mutex).
// SAFE FIX: acquire every relevant mutex in prepare(), release them in both
// parent and child handlers, and keep handlers async-signal-safe.
void bug397_atfork_missing_prepare_lock(void) {
  pthread_atfork(bug397_prepare, bug397_parent, bug397_child);
  fork();
}

static pthread_mutex_t bug398_mu_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t bug398_mu_b = PTHREAD_MUTEX_INITIALIZER;

static void bug398_cleanup(void *arg) {
  (void)arg;
  /* Wrong mutex is unlocked when cancellation unwinds the cleanup stack. */
  pthread_mutex_unlock(&bug398_mu_b);
}

static void *bug398_cancellable(void *arg) {
  (void)arg;
  pthread_mutex_lock(&bug398_mu_a);
  pthread_cleanup_push(bug398_cleanup, NULL);
  pthread_mutex_lock(&bug398_mu_b);
  pthread_mutex_unlock(&bug398_mu_b);
  pthread_cleanup_pop(0);
  pthread_mutex_unlock(&bug398_mu_a);
  return NULL;
}

// BUG 398: a cancellation cleanup handler releases a different mutex from
// the one acquired, leaving mu_a locked forever when the thread is cancelled
// at a cancellation point inside the section.
// SAFE FIX: the cleanup handler must release exactly the resource acquired
// by the protected section (mu_a here), and handlers must pop in order.
void bug398_cleanup_unlocks_wrong_mutex(void) {
  pthread_t t;
  pthread_create(&t, NULL, bug398_cancellable, NULL);
  pthread_cancel(t);
  pthread_join(t, NULL);
}

static pthread_mutex_t bug399_mu_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t bug399_mu_b = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bug399_cv = PTHREAD_COND_INITIALIZER;
static int bug399_ready = 0;

static void *bug399_waiter(void *arg) {
  (void)arg;
  /* Waits while holding mu_a although the predicate is protected by mu_b. */
  pthread_mutex_lock(&bug399_mu_a);
  while (bug399_ready == 0)
    pthread_cond_wait(&bug399_cv, &bug399_mu_a);
  pthread_mutex_unlock(&bug399_mu_a);
  return NULL;
}

static void *bug399_notifier(void *arg) {
  (void)arg;
  pthread_mutex_lock(&bug399_mu_b);
  bug399_ready = 1;
  pthread_cond_signal(&bug399_cv);
  pthread_mutex_unlock(&bug399_mu_b);
  return NULL;
}

// BUG 399: a condition's predicate is guarded by one mutex while the waiter
// passes a different mutex to pthread_cond_wait(); the predicate check and
// the wait are not atomic with respect to the notifier.
// SAFE FIX: use exactly one mutex for both the predicate and the condition
// wait, and pass that same mutex to pthread_cond_wait().
void bug399_predicate_two_mutexes(void) {
  pthread_t w, n;
  pthread_create(&w, NULL, bug399_waiter, NULL);
  pthread_create(&n, NULL, bug399_notifier, NULL);
  pthread_join(w, NULL);
  pthread_join(n, NULL);
}

static pthread_mutex_t bug400_mu = PTHREAD_MUTEX_INITIALIZER;
static int bug400_value = 0;

static void *bug400_trylocker(void *arg) {
  (void)arg;
  /* The return value is ignored: when another thread holds the mutex this
     thread still executes the "critical" section without owning it. */
  pthread_mutex_trylock(&bug400_mu);
  bug400_value++;
  pthread_mutex_unlock(&bug400_mu);
  return NULL;
}

// BUG 400: pthread_mutex_trylock() result is ignored, so the protected update
// runs even when the lock was not acquired (and the later unlock is invalid).
// SAFE FIX: check for 0 (acquired) and only touch shared data on that path;
// handle EBUSY by retrying or by skipping the critical section.
void bug400_trylock_result_ignored(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug400_trylocker, NULL);
  pthread_create(&b, NULL, bug400_trylocker, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static pthread_rwlock_t bug401_rw = PTHREAD_RWLOCK_INITIALIZER;
static void bug401_nested_read(void) {
  pthread_rwlock_rdlock(&bug401_rw);
  pthread_rwlock_unlock(&bug401_rw);
}

// BUG 401: a thread that already holds a read lock recursively acquires the
// same rwlock for reading.  With a writer waiting, many implementations block
// the second rdlock to give the writer priority, causing self-deadlock.
// SAFE FIX: do not recursively acquire read locks; cache the fact that the
// lock is already held, or use a mutex with explicit ownership rules.
void bug401_recursive_read_lock(void) {
  pthread_rwlock_rdlock(&bug401_rw);
  bug401_nested_read();
  pthread_rwlock_unlock(&bug401_rw);
}

static pthread_t bug402_saved;
static int bug402_saved_valid = 0;

static void *bug402_registrar(void *arg) {
  (void)arg;
  /* The thread id and its validity flag are published with plain stores in
     an order another thread can observe out of order. */
  bug402_saved = pthread_self();
  bug402_saved_valid = 1;
  return NULL;
}

// BUG 402: a worker publishes its pthread_t and a validity flag using plain
// non-atomic stores; a supervisor can see the flag before the id and join an
// uninitialised/garbage thread handle.
// SAFE FIX: publish with a single atomic pointer/struct using release, or
// signal readiness through a condition variable after the id is stored.
void bug402_pthread_t_published_racy(void) {
  pthread_t t;
  pthread_create(&t, NULL, bug402_registrar, NULL);
  if (bug402_saved_valid)
    pthread_join(bug402_saved, NULL);
  pthread_join(t, NULL);
}

#if defined(__linux__)
static pthread_barrier_t bug403_barrier;

static void *bug403_barrier_user(void *arg) {
  (void)arg;
  pthread_barrier_wait(&bug403_barrier);
  pthread_barrier_wait(&bug403_barrier); /* called twice by each thread */
  return NULL;
}
#endif

// BUG 403: each worker waits twice on a barrier initialised for a single
// rendezvous, and the creating thread never participates.  The second wait
// can never complete, so all workers block forever.
// SAFE FIX: initialise the barrier with the exact number of participants and
// ensure every participant calls wait exactly once per phase.
void bug403_barrier_count_mismatch(void) {
#if defined(__linux__)
  pthread_barrier_init(&bug403_barrier, NULL, 2);
  pthread_t a, b;
  pthread_create(&a, NULL, bug403_barrier_user, NULL);
  pthread_create(&b, NULL, bug403_barrier_user, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  pthread_barrier_destroy(&bug403_barrier);
#endif
}

#if defined(__linux__)
static sem_t bug404_sem;

static void *bug404_poster(void *arg) {
  (void)arg;
  sem_post(&bug404_sem);
  return NULL;
}
#endif

// BUG 404: sem_wait() is called without retrying when it fails with EINTR.
// A signal arriving during the wait makes the call fail even though no token
// was consumed; ignoring that result loses the synchronisation event.
// SAFE FIX: loop while sem_wait() == -1 && errno == EINTR.
void bug404_sem_wait_eintr(void) {
#if defined(__linux__)
  sem_init(&bug404_sem, 0, 0);
  pthread_t t;
  pthread_create(&t, NULL, bug404_poster, NULL);
  while (sem_wait(&bug404_sem) == -1 && errno == EINTR) {
    /* retry */
  }
  pthread_join(t, NULL);
  sem_destroy(&bug404_sem);
#endif
}

int main(void) { return 0; }
