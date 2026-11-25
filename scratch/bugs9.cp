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
 * 355-364: relaxed atomics, publication and missing acquire/release barriers
 * ============================================================================
 * All bug bodies are intentionally never invoked by main().  They document
 * real C11/POSIX concurrency mistakes; the SAFE FIX comment gives the
 * corresponding correct synchronisation pattern.
 */

static int bug355_payload = 0;
static atomic_int bug355_ready = 0;

static void *bug355_producer(void *arg) {
  (void)arg;
  /* The payload is an ordinary object.  A relaxed store does not order the
     increment above before the flag, so the consumer can observe the flag
     while the payload is still stale. */
  bug355_payload++;
  atomic_store_explicit(&bug355_ready, 1, memory_order_relaxed);
  return NULL;
}

// BUG 355: memory_order_relaxed used to publish a non-atomic payload; the
// consumer has no acquire barrier, so it can read a stale payload after the
// ready flag becomes visible.
// SAFE FIX: store the flag with memory_order_release and load it with
// memory_order_acquire (or make the payload atomic as well).
void bug355_relaxed_publication(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug355_producer, NULL);
  pthread_create(&b, NULL, bug355_producer, NULL);
  while (!atomic_load_explicit(&bug355_ready, memory_order_relaxed)) {
    sched_yield();
  }
  printf("payload=%d\n", bug355_payload);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static atomic_int bug356_ticks = 0;
static int bug356_result = 0;

static void *bug356_worker(void *arg) {
  (void)arg;
  for (int i = 0; i < 100; ++i) {
    /* result is written before the relaxed progress counter is bumped.
       Relaxed ordering permits the counter to become visible first. */
    bug356_result += 1;
    atomic_fetch_add_explicit(&bug356_ticks, 1, memory_order_relaxed);
  }
  return NULL;
}

// BUG 356: a relaxed progress counter is used as the only "work finished"
// signal; the real result can still be invisible when the count is observed.
// SAFE FIX: publish the result with release semantics, or join the threads
// before reading it (join provides the required happens-before edge).
void bug356_relaxed_progress_counter(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug356_worker, NULL);
  pthread_create(&b, NULL, bug356_worker, NULL);
  while (atomic_load_explicit(&bug356_ticks, memory_order_relaxed) < 200) {
    sched_yield();
  }
  printf("result=%d\n", bug356_result);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static atomic_int bug357_stop = 0;
static int bug357_output = 0;

static void *bug357_loop(void *arg) {
  (void)arg;
  while (!atomic_load_explicit(&bug357_stop, memory_order_relaxed)) {
    bug357_output++;
  }
  return NULL;
}

// BUG 357: shutdown handshake uses relaxed loads/stores only.  The thread
// that sets stop can read output before the worker's last write has become
// visible, because no release/acquire pair orders the flush.
// SAFE FIX: store stop with memory_order_release, load it with
// memory_order_acquire in the worker, and join before reading output.
void bug357_relaxed_shutdown(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug357_loop, NULL);
  pthread_create(&b, NULL, bug357_loop, NULL);
  atomic_store_explicit(&bug357_stop, 1, memory_order_relaxed);
  printf("output=%d\n", bug357_output);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

struct bug356_blob {
  int x;
  int y;
};

static _Atomic(struct bug356_blob *) bug358_ptr = NULL;

static void *bug358_publish(void *arg) {
  (void)arg;
  struct bug356_blob *p = (struct bug356_blob *)malloc(sizeof(*p));
  if (p == NULL)
    return NULL;
  /* Publication happens before the object is initialised.  A relaxed store
     gives the reader neither the pointer nor the field values in order. */
  atomic_store_explicit(&bug358_ptr, p, memory_order_relaxed);
  p->x = 7;
  p->y = 9;
  return NULL;
}

// BUG 358: an atomic pointer is stored with relaxed ordering before the
// pointed-to object is fully initialised; another thread can dereference a
// partially constructed object.
// SAFE FIX: initialise all fields first, then publish with memory_order_release
// and consume with memory_order_acquire.
void bug358_publish_before_init(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug358_publish, NULL);
  pthread_create(&b, NULL, bug358_publish, NULL);
  struct bug356_blob *p = atomic_load_explicit(&bug358_ptr, memory_order_relaxed);
  if (p != NULL)
    printf("%d %d\n", p->x, p->y);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static atomic_int bug359_flag_a = 0;
static atomic_int bug359_flag_b = 0;
static int bug359_crit = 0;

static void *bug359_thread_a(void *arg) {
  (void)arg;
  atomic_store_explicit(&bug359_flag_a, 1, memory_order_relaxed);
  /* Missing seq_cst fence here: the two relaxed stores/loads can be
     reordered so both threads observe the other flag as zero. */
  if (atomic_load_explicit(&bug359_flag_b, memory_order_relaxed) == 0)
    bug359_crit++;
  return NULL;
}

// BUG 359: Dekker-style mutual exclusion built from relaxed atomics without
// an intervening sequential-consistency fence; both threads may enter.
// SAFE FIX: place atomic_thread_fence(memory_order_seq_cst) between the store
// and the load in each thread (or use seq_cst atomics throughout).
void bug359_dekker_no_fence(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug359_thread_a, NULL);
  pthread_create(&b, NULL, bug359_thread_a, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  printf("crit=%d\n", bug359_crit);
}

static _Atomic int bug360_counter = 0;

static void *bug360_atomic_writer(void *arg) {
  (void)arg;
  atomic_fetch_add_explicit(&bug360_counter, 1, memory_order_relaxed);
  return NULL;
}

static void *bug360_plain_writer(void *arg) {
  (void)arg;
  /* Type-punning an _Atomic object to a plain int* and writing through it
     is a data race even though the other writer uses atomic operations. */
  int *raw = (int *)&bug360_counter;
  (*raw)++;
  return NULL;
}

// BUG 360: the same object is accessed both atomically and non-atomically;
// mixing the two is undefined behaviour and defeats the atomic guarantee.
// SAFE FIX: perform every access through atomic_* intrinsics on the
// _Atomic-typed object.
void bug360_mixed_atomic_access(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug360_atomic_writer, NULL);
  pthread_create(&b, NULL, bug360_plain_writer, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  printf("counter=%d\n", atomic_load(&bug360_counter));
}

static atomic_int bug361_flag;

static void *bug361_reinit(void *arg) {
  (void)arg;
  /* atomic_init is not an atomic operation.  Re-initialising an object that
     another thread may be reading is a race on the atomic object itself. */
  atomic_init(&bug361_flag, 1);
  return NULL;
}

// BUG 361: atomic_init() used as a runtime "reset" on an atomic object that
// other threads are concurrently loading.
// SAFE FIX: initialise exactly once before publishing the object; to reset,
// use an atomic store with the desired ordering instead of atomic_init.
void bug361_atomic_init_race(void) {
  atomic_init(&bug361_flag, 0);
  pthread_t a, b;
  pthread_create(&a, NULL, bug361_reinit, NULL);
  pthread_create(&b, NULL, bug361_reinit, NULL);
  while (!atomic_load_explicit(&bug361_flag, memory_order_acquire)) {
    sched_yield();
  }
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

struct bug362_payload {
  int a;
  int b;
};

static struct bug362_payload bug362_data = {11, 22};
static _Atomic(struct bug362_payload *) bug362_ptr = NULL;

static void *bug362_publish(void *arg) {
  (void)arg;
  atomic_store_explicit(&bug362_ptr, &bug362_data, memory_order_release);
  return NULL;
}

// BUG 362: memory_order_consume is relied on for ordering even though the
// loaded pointer is not used in a data dependency chain.  Implementations may
// implement consume as relaxed, so the payload reads are unordered.
// SAFE FIX: use memory_order_acquire for the load of the published pointer.
void bug362_consume_without_dependency(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug362_publish, NULL);
  pthread_create(&b, NULL, bug362_publish, NULL);
  struct bug362_payload *p =
      atomic_load_explicit(&bug362_ptr, memory_order_consume);
  int index = (p != NULL) ? 0 : 1; /* control dependency, not a data dependency */
  int value = (index == 0 && p != NULL) ? p->a : -1;
  printf("value=%d\n", value);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

static _Atomic int bug363_owner = 0;
static int bug363_buffer = 0;

static void *bug363_produce(void *arg) {
  (void)arg;
  bug363_buffer = 42;
  /* Handing off ownership with a relaxed exchange gives the new owner no
     guarantee that the buffer write happened before the handoff. */
  atomic_exchange_explicit(&bug363_owner, 1, memory_order_relaxed);
  return NULL;
}

// BUG 363: relaxed atomic_exchange used to transfer ownership of a buffer;
// the receiving thread can observe an uninitialised buffer.
// SAFE FIX: use memory_order_acq_rel (release the data, acquire the token)
// or seq_cst for the exchange.
void bug363_relaxed_ownership_handoff(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug363_produce, NULL);
  pthread_create(&b, NULL, bug363_produce, NULL);
  while (atomic_load_explicit(&bug363_owner, memory_order_relaxed) == 0) {
    sched_yield();
  }
  printf("buffer=%d\n", bug363_buffer);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

#define BUG364_SLOTS 8
static int bug364_slots[BUG364_SLOTS];
static atomic_int bug364_head = 0;

static void *bug364_producer(void *arg) {
  (void)arg;
  for (int i = 0; i < BUG364_SLOTS; ++i) {
    bug364_slots[i] = i + 1;
    /* Relaxed publication of the ring head: the consumer may see the new
       head before the slot write, or keep seeing the stale head forever. */
    atomic_store_explicit(&bug364_head, i + 1, memory_order_relaxed);
  }
  return NULL;
}

// BUG 364: a single-producer/single-consumer ring buffer publishes its head
// with a relaxed store, so the consumer has no acquire edge for the slot data.
// SAFE FIX: store head with memory_order_release and load it with
// memory_order_acquire.
void bug364_relaxed_ring_head(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, bug364_producer, NULL);
  pthread_create(&b, NULL, bug364_producer, NULL);
  int h = atomic_load_explicit(&bug364_head, memory_order_relaxed);
  if (h > 0)
    printf("slot=%d\n", bug364_slots[h - 1]);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
}

int main(void) { return 0; }
