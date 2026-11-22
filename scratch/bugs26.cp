#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Teaching catalog: allocator calls are indirected through function pointers
   so the intentionally-broken examples still build under C+ default checks. */
static void (*cat_free)(void *) = free;
static void *(*cat_realloc)(void *, size_t) = realloc;

// ============================================================================
// BUG 525: pointer into alloca-style automatic storage returned to caller
// ============================================================================
int *bug525_alloca_storage_returned(void) {
  int scratch[16];
  int **holder = (int **)malloc(sizeof(int *));
  if (!holder)
    return NULL;
  *holder = scratch;
  // BUG: scratch has automatic duration (as with alloca storage); returning
  // its address leaves the caller with a dangling pointer.
  return *holder;
}
// SAFE FIX: return heap-owned memory or require caller-owned storage.

// ============================================================================
// BUG 526: freeing alloca-style automatic storage
// ============================================================================
void bug526_alloca_storage_freed(void) {
  char buf[64];
  // BUG: automatic storage is reclaimed at scope exit; a deallocator call on
  // it (as often done by mistake with alloca blocks) corrupts the heap.
  cat_free((void *)buf);
}
// SAFE FIX: never deallocate automatic storage.

// ============================================================================
// BUG 527: unbounded alloca-style stack allocation from user input
// ============================================================================
void bug527_alloca_unbounded_size(size_t claimed) {
  /* Stand-in for alloca(claimed): a large automatic request with no bound. */
  unsigned char frame[256];
  // BUG: with alloca, a huge claimed size overruns the stack with no failure
  // signal (unlike malloc which returns NULL).
  (void)claimed;
  frame[0] = 0;
}
// SAFE FIX: cap the size and prefer heap allocation with a NULL check.

// ============================================================================
// BUG 528: arena reset while pointers into it are still live
// ============================================================================
struct bug528_arena {
  unsigned char buf[4096];
  size_t used;
};
void bug528_arena_reset_with_live_ptr(void) {
  struct bug528_arena a = {{0}, 0};
  int *live = (int *)(void *)a.buf;
  a.used = 0; /* arena reset */
  // BUG: 'live' still points into the arena and may now be overwritten.
  *live = 3;
}
// SAFE FIX: invalidate all arena pointers on reset, or use a generation tag.

// ============================================================================
// BUG 529: using an allocation after its arena is destroyed
// ============================================================================
static unsigned char bug529_arena[512];
void bug529_arena_destroy_then_use(void) {
  int *p = (int *)(void *)bug529_arena;
  /* Arena teardown: memory is released or reused for other objects. */
  // BUG: p is used after the arena's lifetime ends.
  *p = 9;
}
// SAFE FIX: tie arena allocations to the arena lifetime and null them on teardown.

// ============================================================================
// BUG 530: allocation size underflow from header subtraction
// ============================================================================
void bug530_size_underflow_header(size_t want) {
  size_t header = 64;
  size_t payload = want - header;
  // BUG: if want < header the subtraction wraps to a huge size, producing a
  // failed or wildly oversized allocation.
  void *p = malloc(payload);
  cat_free(p);
}
// SAFE FIX: check want >= header before subtracting.

// ============================================================================
// BUG 531: allocation size underflow from capacity-minus-used
// ============================================================================
void bug531_size_underflow_index(size_t capacity, size_t used) {
  size_t free_slots = capacity - used;
  // BUG: used > capacity wraps free_slots; the allocation is astronomically
  // large and the subsequent write is out of range.
  int *extra = (int *)malloc(free_slots * sizeof(int));
  if (extra)
    extra[0] = 1;
  cat_free(extra);
}
// SAFE FIX: validate used <= capacity and use checked subtraction.

// ============================================================================
// BUG 532: allocating with sizeof(pointer) instead of sizeof(object)
// ============================================================================
void bug532_sizeof_pointer_instead_of_object(size_t n) {
  char **slots = (char **)malloc(n * sizeof(char *));
  // BUG: the array is meant to hold 'n' char objects, but sizeof(char*) bytes
  // were requested per element; for small element types this over-allocates,
  // for larger ones it truncates. Treating slots as char* below reads garbage.
  if (slots)
    slots[0] = NULL;
  cat_free(slots);
}
// SAFE FIX: allocate n * sizeof(*slots) for a pointer array, or n bytes for
// a char array, matching the intended element type.

// ============================================================================
// BUG 533: flexible array member allocated with only the header size
// ============================================================================
struct bug533_flex {
  size_t len;
  int data[];
};
void bug533_flex_array_underalloc(size_t n) {
  struct bug533_flex *f = (struct bug533_flex *)malloc(sizeof(*f));
  if (!f)
    return;
  f->len = n;
  // BUG: only the header was allocated; data[] has no backing storage.
  f->data[0] = 1;
  cat_free(f);
}
// SAFE FIX: allocate sizeof(*f) + n * sizeof(f->data[0]).

// ============================================================================
// BUG 534: flexible array allocated one element short
// ============================================================================
struct bug534_flex {
  size_t len;
  int data[];
};
void bug534_flex_array_off_by_one(size_t n) {
  struct bug534_flex *f =
      (struct bug534_flex *)malloc(sizeof(*f) + n * sizeof(int) - 1);
  if (!f)
    return;
  f->len = n;
  // BUG: one int short; writing data[n-1] overflows the block.
  f->data[n - 1] = 7;
  cat_free(f);
}
// SAFE FIX: allocate exactly n elements; avoid ad-hoc byte fudging.

int main(void) {
  return 0;
}
