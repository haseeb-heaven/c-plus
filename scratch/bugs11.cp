#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 375-384: ABA, lock-free publication, thread identity and per-thread state
 * ============================================================================
 * None of these functions is invoked from main().  Each SAFE FIX comment
 * describes the algorithm change that removes the described failure.
 */

struct bug375_node {
  int value;
  uint32_t generation;
  struct bug375_node *next;
};

static _Atomic(struct bug375_node *) bug375_top = NULL;
static int bug375_pop_count = 0;

static void *bug375_popper(void *arg) {
  (void)arg;
  struct bug375_node *old =
      atomic_load_explicit(&bug375_top, memory_order_relaxed);
  struct bug375_node *next = (old != NULL) ? old->next : NULL;
  /* A 32-bit generation can wrap: after 2^32 pop/push cycles the ABA tag is
     identical and this CAS succeeds on a node that was removed and reused. */
  bug375_pop_count++;
  atomic_compare_exchange_weak_explicit(&bug375_top, &old, next,
                                        memory_order_acq_rel,
                                        memory_order_relaxed);
  return NULL;
}

// BUG 375: lock-free stack pop uses a 32-bit generation tag to defeat ABA;
// the tag wraps after 2^32 operations, restoring the original pointer/tag
// pair and letting a stale CAS succeed.
// SAFE FIX: use a 64-bit (or wider) tag, or a hazard-pointer/epoch scheme that
// never reuses a node while a CAS may still reference it.
void bug375_aba_generation_wrap(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug375_popper, NULL);
  pthread_create(&b, NULL, bug375_popper, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  printf("pops=%d\n", bug375_pop_count);
}

struct bug376_node {
  int value;
  struct bug376_node *next;
};

static _Atomic(struct bug376_node *) bug376_head = NULL;
static _Atomic(struct bug376_node *) bug376_hazard = NULL;

static void bug376_reclaim(struct bug376_node *n) {
  /* A hazard pointer must be re-read and the list re-validated after the
     second scan; a single check is not enough. */
  if (atomic_load_explicit(&bug376_hazard, memory_order_acquire) != n)
    free(n);
}

static void *bug376_scanner(void *arg) {
  (void)arg;
  struct bug376_node *n =
      atomic_load_explicit(&bug376_head, memory_order_acquire);
  atomic_store_explicit(&bug376_hazard, n, memory_order_seq_cst);
  /* Missing: re-load head and confirm n is still reachable before freeing. */
  bug376_reclaim(n);
  return NULL;
}

// BUG 376: hazard-pointer reclamation frees a node after a single hazard
// scan; the node can be unlinked and freed by another thread between the
// scan and the dereference.
// SAFE FIX: after publishing the hazard pointer, re-scan the shared structure
// and only retire the node if it is still unreachable by every hazard slot.
void bug376_hazard_not_revalidated(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug376_scanner, NULL);
  pthread_create(&b, NULL, bug376_scanner, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

struct bug377_node {
  int value;
  struct bug377_node *next; /* plain, non-atomic link */
};

static struct bug377_node *bug377_queue_head = NULL;

static void *bug377_producer(void *arg) {
  (void)arg;
  struct bug377_node *n = (struct bug377_node *)malloc(sizeof(*n));
  if (n == NULL)
    return NULL;
  n->value = 1;
  n->next = bug377_queue_head;
  /* Non-atomic publication of a pointer written concurrently by two
     producers: readers can observe a torn or stale link value. */
  bug377_queue_head = n;
  return NULL;
}

// BUG 377: a lock-free queue stores its "next" links in ordinary pointers;
// concurrent producers create a data race and readers can see torn pointers.
// SAFE FIX: make every shared link an _Atomic pointer and use release/acquire
// compare-exchange operations.
void bug377_nonatomic_next_link(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug377_producer, NULL);
  pthread_create(&b, NULL, bug377_producer, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  struct bug377_node *p = bug377_queue_head;
  if (p != NULL)
    printf("value=%d\n", p->value);
}

static _Atomic(struct bug376_node *) bug378_stack = NULL;

static void *bug378_single_cas(void *arg) {
  (void)arg;
  struct bug376_node *old =
      atomic_load_explicit(&bug378_stack, memory_order_relaxed);
  struct bug376_node *desired =
      (struct bug376_node *)malloc(sizeof(struct bug376_node));
  if (desired == NULL)
    return NULL;
  desired->value = 3;
  desired->next = old;
  /* compare_exchange_weak may fail spuriously.  With no retry loop the node
     is lost and the intended push never happens. */
  atomic_compare_exchange_weak_explicit(&bug378_stack, &old, desired,
                                        memory_order_release,
                                        memory_order_relaxed);
  return NULL;
}

// BUG 378: atomic_compare_exchange_weak() is called once, without retrying
// spurious failures, so a push can silently disappear and leak the node.
// SAFE FIX: wrap weak CAS in a do/while loop that reloads the expected value,
// or use atomic_compare_exchange_strong() when retrying is not desired.
void bug378_weak_cas_no_retry(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug378_single_cas, NULL);
  pthread_create(&b, NULL, bug378_single_cas, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

// BUG 379: pthread_join() is called on the calling thread itself.  POSIX
// returns EDEADLK for a normal joinable thread; ignoring the result leaves
// the program believing it synchronised when it did not.
// SAFE FIX: only join other threads, or detach the current thread and use a
// condition variable/atomic flag to wait for the desired state.
void bug379_join_self(void) {
  int rc = pthread_join(pthread_self(), NULL);
  printf("join rc=%d\n", rc);
}

static int bug380_values[2];

static void *bug380_reader(void *arg) {
  (void)arg;
  /* All workers receive the address of the same loop slot.  By the time a
     worker reads it, the loop has advanced and the value changed. */
  printf("value=%d\n", bug380_values[0]);
  return NULL;
}

// BUG 380: the address of a loop-local variable (same stack slot on every
// iteration) is passed to several threads; they all observe the final
// iteration's value rather than their own.
// SAFE FIX: allocate one argument object per thread (heap or distinct array
// element) and pass that address, or pass the value directly via arg.
void bug380_loop_local_address_to_threads(void) {
  pthread_t t[2];
  for (int i = 0; i < 2; ++i) {
    bug380_values[0] = i;
    pthread_create(&t[i], NULL, bug380_reader, &bug380_values[0]);
  }
  pthread_join(t[0], NULL);
  pthread_join(t[1], NULL);
}

static pthread_key_t bug381_key;

struct bug381_cache {
  int fd;
};

static void bug381_destructor(void *p) {
  free(p); /* runs on thread exit, possibly while another thread cached p */
}

static void *bug381_cached_reader(void *arg) {
  (void)arg;
  struct bug381_cache *c =
      (struct bug381_cache *)pthread_getspecific(bug381_key);
  if (c != NULL)
    printf("fd=%d\n", c->fd);
  return NULL;
}

static void *bug381_owner(void *arg) {
  (void)arg;
  struct bug381_cache *c = (struct bug381_cache *)malloc(sizeof(*c));
  if (c != NULL) {
    c->fd = 3;
    pthread_setspecific(bug381_key, c);
  }
  return NULL;
}

// BUG 381: a TLS destructor frees an object while another thread holds and
// uses a cached pointer to it, producing a use-after-free at thread exit.
// SAFE FIX: never share TLS-owned objects by raw pointer; use reference
// counting or copy the data into memory owned by the consuming thread.
void bug381_tls_destructor_uaf(void) {
  pthread_key_create(&bug381_key, bug381_destructor);
  pthread_t owner, reader;
  pthread_create(&owner, NULL, bug381_owner, NULL);
  pthread_join(owner, NULL); /* destructor frees the cached object */
  pthread_create(&reader, NULL, bug381_cached_reader, NULL);
  pthread_join(reader, NULL);
  pthread_key_delete(bug381_key);
}

static pthread_t bug382_a;
static pthread_t bug382_b;

// BUG 382: pthread_t values are compared with == instead of pthread_equal().
// pthread_t is opaque and may be a struct; == is not portable and can give
// the wrong answer (or fail to compile) on such implementations.
// SAFE FIX: always use pthread_equal(t1, t2) to compare thread identifiers.
void bug382_pthread_t_equality(void) {
  bug382_a = pthread_self();
  bug382_b = pthread_self();
  if (bug382_a == bug382_b)
    printf("same thread\n");
}

static pthread_key_t bug383_key;
static pthread_t bug383_worker;

static void *bug383_delayed(void *arg) {
  (void)arg;
  /* This thread still has TLS state for the key when the key is deleted. */
  void *v = pthread_getspecific(bug383_key);
  printf("v=%p\n", v);
  return NULL;
}

// BUG 383: pthread_key_delete() is called while a thread that still uses the
// key is running; its destructor will not run and its TLS slot is orphaned.
// SAFE FIX: join every thread that can use the key before deleting it, or
// coordinate shutdown so no thread touches the key afterwards.
void bug383_key_delete_too_early(void) {
  pthread_key_create(&bug383_key, NULL);
  pthread_create(&bug383_worker, NULL, bug383_delayed, NULL);
  pthread_key_delete(bug383_key);
  pthread_join(bug383_worker, NULL);
}

static int bug384_local = 0;

static void *bug384_exiter(void *arg) {
  (void)arg;
  /* Returning the address of a stack local from pthread_exit makes the joiner
     read a dangling pointer as soon as the thread's stack is reclaimed. */
  pthread_exit((void *)&bug384_local);
  return NULL;
}

// BUG 384: pthread_exit() is given the address of a stack-local variable as
// the thread's return value; the joining thread dereferences freed stack.
// SAFE FIX: return a heap-allocated value (freed by the joiner), a static
// value, or a value written through a caller-provided pointer.
void bug384_pthread_exit_stack_return(void) {
  pthread_t t;
  void *result = NULL;
  pthread_create(&t, NULL, bug384_exiter, NULL);
  pthread_join(t, &result);
  printf("local=%d\n", *(int *)result);
}

int main(void) { return 0; }
