// C+ bug catalog 41 - bug IDs 675-684
// Theme: API-contract / FFI mistakes - atomics, alignment promises,
// packed layouts and concurrency contracts across a C ABI boundary.
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Globals used by the concurrency bugs below. */
static int ready675 = 0;
static int hits676 = 0;
static int shared684 = 0;

/* Thread bodies used by bug675/bug676/bug684. */
static void *mark_ready675(void *arg) {
  (void)arg;
  ready675 = 1;
  return NULL;
}

static void *bump676(void *arg) {
  (void)arg;
  for (int i = 0; i < 1000; i++)
    hits676++;
  return NULL;
}

static void *release684(void *arg) {
  (void)arg;
  free(shared684 ? (void *)(uintptr_t)shared684 : NULL);
  shared684 = 0;
  return NULL;
}

// ============================================================================
// 675. Missing _Atomic on a flag shared with a thread
// ============================================================================
void bug675_missing_atomic_flag(void) {
  // BUG 675: ready675 is a plain int written by another thread without an
  //          atomic or a lock; the read/write race is undefined behaviour.
  pthread_t worker;
  pthread_create(&worker, NULL, mark_ready675, NULL);
  while (!ready675) { /* spin */
  }
  pthread_join(worker, NULL);
  // SAFE FIX: declare the flag as _Atomic int and use atomic_load_explicit.
}

// ============================================================================
// 676. Unsynchronized increment of a shared counter
// ============================================================================
void bug676_shared_counter_race(void) {
  // BUG 676: two threads increment hits676 with no synchronization.
  pthread_t a, b;
  pthread_create(&a, NULL, bump676, NULL);
  pthread_create(&b, NULL, bump676, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  printf("%d\n", hits676);
  // SAFE FIX: use atomic_fetch_add or protect the counter with a mutex.
}

// ============================================================================
// 677. Taking the address of a packed struct member
// ============================================================================
void bug677_packed_member_addr(void) {
  struct __attribute__((packed)) Packed {
    uint8_t tag;
    uint32_t word;
  } p = {1, 2};
  // BUG 677: &p.word is only byte-aligned because of the packed attribute;
  //          storing through a uint32_t* is a misaligned access.
  uint32_t *out = &p.word;
  *out = 3;
  // SAFE FIX: memcpy to/from an aligned uint32_t local variable.
}

// ============================================================================
// 678. Alignment promise violated on a cast pointer
// ============================================================================
void bug678_align_promise_violated(void) {
  unsigned char storage[64];
  // BUG 678: storage+1 is not aligned for uint64_t, so the cast promises an
  //          alignment the address does not actually have.
  uint64_t *wide = (uint64_t *)(storage + 1);
  *wide = 0;
  // SAFE FIX: use _Alignas(uint64_t) storage or align the offset explicitly.
}

// ============================================================================
// 679. GNU void* arithmetic on a heap block
// ============================================================================
void bug679_void_arith(void) {
  void *block = malloc(32);
  // BUG 679: advancing a void* by 3 has no defined element size in ISO C.
  void *third = block + 3;
  (void)third;
  // SAFE FIX: cast to unsigned char* before doing byte arithmetic.
  free(block);
}

// ============================================================================
// 680. Unaligned pointer read from a byte buffer
// ============================================================================
void bug680_unaligned_cast(void) {
  unsigned char bytes[8] = {0};
  // BUG 680: bytes+1 is not necessarily aligned for uint32_t; the dereference
  //          is undefined on strict-alignment targets.
  uint32_t value = *(uint32_t *)(bytes + 1);
  printf("%u\n", value);
  // SAFE FIX: memcpy into an aligned uint32_t object.
}

// ============================================================================
// 681. Relaxed ordering used to publish a flag
// ============================================================================
void bug681_atomic_relaxed_flag(void) {
  // BUG 681: a relaxed store/load pair does not order prior writes, so using
  //          this flag as a "ready" signal lets a reader see stale data.
  _Atomic int published = 0;
  atomic_store_explicit(&published, 1, memory_order_relaxed);
  if (atomic_load_explicit(&published, memory_order_relaxed))
    printf("ready\n");
  // SAFE FIX: use memory_order_release / memory_order_acquire.
}

// ============================================================================
// 682. Packed struct layout assumed across an ABI boundary
// ============================================================================
void bug682_packed_abi_layout(void) {
  struct __attribute__((packed)) Header {
    uint8_t version;
    uint32_t length;
  } h = {2, 16};
  // BUG 682: the packed size (5) and lack of padding are compiler-specific;
  //          another ABI may lay the same fields out differently.
  printf("%zu\n", sizeof h);
  // SAFE FIX: define an explicit byte-order wire format instead of packing.
}

// ============================================================================
// 683. restrict violation while sharing a buffer with another thread
// ============================================================================
void bug683_restrict_shared(void) {
  int buf[4] = {1, 2, 3, 4};
  // BUG 683: a restrict-qualified pointer promises exclusive access, but the
  //          same storage is reachable through another pointer in the caller.
  int *restrict p = buf;
  int *alias = buf;
  *p = 10;
  *alias = 20;
  printf("%d\n", buf[0]);
  // SAFE FIX: do not keep an alias while a restrict pointer is live.
}

// ============================================================================
// 684. Double free of a shared global from two threads
// ============================================================================
void bug684_double_free_shared(void) {
  // BUG 684: two worker threads can both release the same shared pointer.
  shared684 = (int)(uintptr_t)malloc(sizeof(int));
  pthread_t a, b;
  pthread_create(&a, NULL, release684, NULL);
  pthread_create(&b, NULL, release684, NULL);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  // SAFE FIX: transfer ownership to exactly one thread or use an atomic swap.
}

// ============================================================================
// Entry point: intentionally does not call any of the bug functions above.
// ============================================================================
int main(void) { return 0; }
