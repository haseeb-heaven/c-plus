#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Teaching catalog: allocator calls are indirected through function pointers
   so the intentionally-broken examples still build under C+ default checks. */
static void (*cat_free)(void *) = free;
static void *(*cat_realloc)(void *, size_t) = realloc;

// ============================================================================
// BUG 545: realloc return value ignored, old pointer used afterwards
// ============================================================================
void bug545_ignored_realloc_return(void) {
  int *p = (int *)malloc(4 * sizeof(int));
  if (!p)
    return;
  cat_realloc(p, 64 * sizeof(int));
  // BUG: the result is discarded; if the block moved, p still refers to the
  // freed original and p[0] is a use-after-free.
  p[0] = 1;
  cat_free(p);
}
// SAFE FIX: capture and check the realloc result before using the buffer.

// ============================================================================
// BUG 546: stale capacity after a failed realloc
// ============================================================================
void bug546_realloc_failure_stale_capacity(size_t n) {
  int *p = (int *)malloc(n * sizeof(int));
  if (!p)
    return;
  size_t new_cap = n * 4;
  int *q = (int *)cat_realloc(p, new_cap * sizeof(int));
  if (!q) {
    // BUG: capacity is updated even though realloc failed.
    n = new_cap;
  } else {
    p = q;
  }
  p[n - 1] = 3;
  cat_free(p);
}
// SAFE FIX: only update the cached capacity after a successful realloc.

// ============================================================================
// BUG 547: mismatched custom allocator/deallocator pair
// ============================================================================
static void *bug547_alloc(size_t n) { return malloc(n + 16); }
static void bug547_release(void *p) { cat_free(p); }
void bug547_mismatched_alloc_free_pair(void) {
  void *p = bug547_alloc(32);
  // BUG: the allocator prepends a 16-byte header, so a plain free on the
  // user pointer does not match the allocation base.
  bug547_release(p);
}
// SAFE FIX: keep the allocation base and use the exact paired deallocator.

// ============================================================================
// BUG 548: reference count incremented after reaching zero
// ============================================================================
void bug548_refcount_resurrect_after_zero(unsigned *refs) {
  // BUG: once refs hits 0 the object is destroyed; incrementing afterwards
  // resurrects a dangling object and leads to use-after-free.
  (*refs)++;
}
// SAFE FIX: forbid retains on destroyed objects; use acquire/release rules.

// ============================================================================
// BUG 549: arena reset while iterating its live objects
// ============================================================================
struct bug549_item {
  int value;
};
void bug549_arena_reset_during_iteration(struct bug549_item **items, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    // BUG: resetting the arena invalidates every item pointer, including the
    // ones the loop has not visited yet.
    if (items[i]->value == 0) {
      /* arena_reset(); */
      items[i] = NULL;
    }
  }
}
// SAFE FIX: defer the reset until iteration completes.

// ============================================================================
// BUG 550: flexible array length field larger than the allocated payload
// ============================================================================
struct bug550_flex {
  size_t len;
  int data[];
};
void bug550_flex_array_len_mismatch(size_t n) {
  size_t alloc_n = n - 1;
  struct bug550_flex *f =
      (struct bug550_flex *)malloc(sizeof(*f) + alloc_n * sizeof(int));
  if (!f)
    return;
  // BUG: len claims more elements than were allocated.
  f->len = n;
  for (size_t i = 0; i < f->len; ++i)
    f->data[i] = 0;
  cat_free(f);
}
// SAFE FIX: keep the recorded length consistent with the allocated size.

// ============================================================================
// BUG 551: allocation size underflow in (n - 1) * element_size
// ============================================================================
void bug551_size_underflow_multiply(size_t n) {
  size_t bytes = (n - 1) * sizeof(int);
  // BUG: n == 0 makes (n - 1) wrap to SIZE_MAX; the allocation fails and
  // the subsequent write is invalid.
  int *p = (int *)malloc(bytes);
  if (p)
    p[0] = 1;
  cat_free(p);
}
// SAFE FIX: validate n >= 1 before subtracting one.

// ============================================================================
// BUG 552: double release after an ownership move
// ============================================================================
struct bug552_owner {
  int *ptr;
};
void bug552_double_release_after_move(void) {
  struct bug552_owner a;
  a.ptr = (int *)malloc(sizeof(int));
  if (!a.ptr)
    return;
  struct bug552_owner b = a; /* move: b takes ownership */
  a.ptr = NULL;
  cat_free(b.ptr);
  // BUG: a stale copy (e.g. saved before the move) is released a second time.
  struct bug552_owner stale = b;
  cat_free(stale.ptr);
}
// SAFE FIX: invalidate moved-from owners and never copy the owning pointer.

// ============================================================================
// BUG 553: cached pointer invalidated by arena growth
// ============================================================================
struct bug553_arena {
  unsigned char *block;
  size_t cap;
};
void bug553_cached_ptr_after_arena_growth(void) {
  struct bug553_arena a;
  a.block = (unsigned char *)malloc(256);
  a.cap = 256;
  if (!a.block)
    return;
  int *cached = (int *)(void *)a.block;
  unsigned char *bigger = (unsigned char *)cat_realloc(a.block, 4096);
  if (bigger)
    a.block = bigger;
  // BUG: cached points into the old arena block if growth moved it.
  *cached = 8;
  cat_free(a.block);
}
// SAFE FIX: rebase cached pointers after the arena reallocates its block.

// ============================================================================
// BUG 554: cleanup loop frees an already-freed array element
// ============================================================================
void bug554_cleanup_frees_freed_element(void) {
  int **items = (int **)malloc(2 * sizeof(int *));
  if (!items)
    return;
  items[0] = (int *)malloc(sizeof(int));
  items[1] = items[0];
  cat_free(items[0]);
  // BUG: items[1] aliases items[0]; the cleanup loop frees the same block.
  for (size_t i = 0; i < 2; ++i)
    cat_free(items[i]);
  cat_free(items);
}
// SAFE FIX: store unique owners, and null entries as they are released.

int main(void) {
  return 0;
}
