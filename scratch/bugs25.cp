#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Teaching catalog: allocator/deallocator calls are indirected through
   function pointers so the intentionally-broken examples still build. */
static void (*cat_free)(void *) = free;
static void *(*cat_realloc)(void *, size_t) = realloc;
static void *(*cat_malloc)(size_t) = malloc;
static void *(*cat_calloc)(size_t, size_t) = calloc;

// ============================================================================
// BUG 515: zero-size allocation then dereference
// ============================================================================
void bug515_zero_size_malloc_deref(void) {
  size_t zero = 0;
  int *p = (int *)cat_malloc(zero);
  // BUG: malloc(0) may return NULL or a zero-size block; writing through it
  // is undefined behavior.
  if (p)
    *p = 1;
  cat_free(p);
}
// SAFE FIX: reject zero requests, or allocate at least one element.

// ============================================================================
// BUG 516: calloc with a zero dimension then dereference
// ============================================================================
void bug516_zero_size_calloc_deref(void) {
  size_t zero = 0;
  long *p = (long *)cat_calloc(zero, sizeof(long));
  // BUG: a zero dimension yields a zero-size allocation; p[0] is invalid.
  if (p)
    p[0] = 42;
  cat_free(p);
}
// SAFE FIX: validate both calloc dimensions before use.

// ============================================================================
// BUG 517: realloc(ptr, 0) frees the block, then old pointer is written
// ============================================================================
void bug517_realloc_zero_then_write(void) {
  size_t zero = 0;
  int *p = (int *)malloc(sizeof(int));
  if (!p)
    return;
  cat_realloc(p, zero);
  // BUG: realloc(ptr, 0) releases the original block; p is now dangling.
  *p = 5;
}
// SAFE FIX: treat realloc(ptr, 0) as a free and never touch the old pointer.

// ============================================================================
// BUG 518: block from a custom pool freed with the C library deallocator
// ============================================================================
static unsigned char bug518_pool[1024];
static size_t bug518_used;
static void *bug518_pool_alloc(size_t n) {
  void *p = bug518_pool + bug518_used;
  bug518_used += (n + 7u) & ~7u;
  return p;
}
void bug518_wrong_free_for_pool_block(void) {
  void *p = bug518_pool_alloc(32);
  // BUG: p came from a static arena; the C allocator cannot free it.
  cat_free(p);
}
// SAFE FIX: return pool memory with the pool's own release routine (or do
// nothing for a bump allocator).

// ============================================================================
// BUG 519: over-aligned block released with the wrong deallocator
// ============================================================================
static void *(*bug519_aligned_alloc)(size_t, size_t) = aligned_alloc;
static void (*bug519_aligned_release)(void *, size_t) = NULL;
void bug519_wrong_deallocator_for_aligned(void) {
  void *p = bug519_aligned_alloc(64, 64);
  if (!p)
    return;
  // BUG: an over-aligned block needs its matching release routine; passing it
  // to the generic deallocator loses the alignment/cookie contract.
  cat_free(p);
  (void)bug519_aligned_release;
}
// SAFE FIX: pair every custom allocation with its exact deallocator.

// ============================================================================
// BUG 520: double free via a copied handle
// ============================================================================
struct bug520_handle {
  int *ptr;
};
void bug520_double_free_copied_handle(void) {
  struct bug520_handle a;
  a.ptr = (int *)malloc(sizeof(int));
  if (!a.ptr)
    return;
  struct bug520_handle b = a;
  cat_free(a.ptr);
  // BUG: b is a bitwise copy of a and owns the same pointer.
  cat_free(b.ptr);
}
// SAFE FIX: define a single owner and use moves/borrowing for other handles.

// ============================================================================
// BUG 521: double free across an error path and the common path
// ============================================================================
void bug521_double_free_error_path(int fail) {
  int *p = (int *)malloc(sizeof(int));
  if (!p)
    return;
  if (fail) {
    cat_free(p);
    // Missing return falls through to the common cleanup below.
  }
  cat_free(p);
  // BUG: on the error path p is released twice.
}
// SAFE FIX: set the pointer to NULL after freeing, or return from each branch.

// ============================================================================
// BUG 522: freeing a string literal
// ============================================================================
void bug522_free_string_literal(void) {
  const char *msg = "read-only literal";
  // BUG: msg points to static read-only storage, not the heap.
  cat_free((void *)msg);
}
// SAFE FIX: only free memory obtained from an allocator; copy first if needed.

// ============================================================================
// BUG 523: freeing a pointer to const static storage
// ============================================================================
static const int bug523_const_static = 7;
void bug523_free_const_static(void) {
  const int *p = &bug523_const_static;
  // BUG: static const storage is not dynamically allocated.
  cat_free((void *)p);
}
// SAFE FIX: never pass static/const addresses to a deallocator.

// ============================================================================
// BUG 524: freeing an mmap region with the heap deallocator
// ============================================================================
void bug524_free_mmap_region(void *addr, size_t len) {
  (void)len;
  // BUG: memory obtained via mmap must be released with munmap; free() will
  // corrupt the C allocator's metadata.
  cat_free(addr);
}
// SAFE FIX: match mmap with munmap and malloc with free.

int main(void) {
  return 0;
}
