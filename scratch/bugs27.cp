#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Teaching catalog: allocator calls are indirected through function pointers
   so the intentionally-broken examples still build under C+ default checks. */
static void (*cat_free)(void *) = free;
static void *(*cat_realloc)(void *, size_t) = realloc;

// ============================================================================
// BUG 535: unsigned reference count underflows on release
// ============================================================================
void bug535_refcount_wrap_decrement(unsigned *refs) {
  // BUG: releasing when refs == 0 wraps to UINT_MAX; the object is never
  // destroyed and later decrements wrap again.
  (*refs)--;
}
// SAFE FIX: assert refs > 0 before decrementing.

// ============================================================================
// BUG 536: double free through two copied aliases
// ============================================================================
static int *bug536_global_alias;
void bug536_double_free_two_aliases(void) {
  int *p = (int *)malloc(sizeof(int));
  if (!p)
    return;
  bug536_global_alias = p;
  cat_free(p);
  // BUG: the global alias still refers to the freed block.
  cat_free(bug536_global_alias);
}
// SAFE FIX: null every alias when ownership is released.

// ============================================================================
// BUG 537: unsigned reference count overflows on retain
// ============================================================================
void bug537_refcount_wrap_increment(unsigned *refs) {
  // BUG: overflowing to 0 makes the next release destroy the object while
  // other owners still hold references.
  (*refs)++;
}
// SAFE FIX: saturate or fail on overflow; use a checked increment.

// ============================================================================
// BUG 538: callee frees a borrowed argument, caller frees it again
// ============================================================================
void bug538_callee_frees_borrowed(int *p) {
  cat_free(p);
  // BUG: the API contract says the pointer is borrowed, but it is released.
}
void bug538_ownership_cross_boundary(void) {
  int *p = (int *)malloc(sizeof(int));
  if (!p)
    return;
  bug538_callee_frees_borrowed(p);
  cat_free(p); // caller still believes it owns the block
}
// SAFE FIX: document and honor a single owner across the API boundary.

// ============================================================================
// BUG 539: caller frees while callee still uses the pointer
// ============================================================================
void bug539_use_after_caller_free(int *p) {
  // BUG: by the time this helper runs, the caller has already freed p.
  *p = 11;
}
void bug539_ownership_caller_frees_first(void) {
  int *p = (int *)malloc(sizeof(int));
  if (!p)
    return;
  cat_free(p);
  bug539_use_after_caller_free(p);
}
// SAFE FIX: transfer ownership explicitly or copy before releasing.

// ============================================================================
// BUG 540: API caches a caller pointer, caller then frees it
// ============================================================================
static int *bug540_cached;
void bug540_store_borrowed(int *p) { bug540_cached = p; }
void bug540_ownership_stored_then_freed(void) {
  int *p = (int *)malloc(sizeof(int));
  if (!p)
    return;
  bug540_store_borrowed(p);
  cat_free(p);
  // BUG: bug540_cached is now a dangling pointer held by the API.
}
// SAFE FIX: copy on store, or share ownership with a refcount.

// ============================================================================
// BUG 541: struct field caches a pointer into a realloc'd buffer
// ============================================================================
struct bug541_view {
  int *base;
  int *mark;
};
void bug541_stale_ptr_in_struct(void) {
  int *buf = (int *)malloc(4 * sizeof(int));
  if (!buf)
    return;
  struct bug541_view v = {buf, buf + 1};
  int *nb = (int *)cat_realloc(buf, 64 * sizeof(int));
  if (nb)
    buf = nb;
  // BUG: v.mark may point into the old, freed buffer.
  *v.mark = 2;
  cat_free(buf);
}
// SAFE FIX: recompute cached pointers from the new base after realloc.

// ============================================================================
// BUG 542: array of interior pointers invalidated by realloc
// ============================================================================
void bug542_stale_ptr_array_after_realloc(void) {
  int *buf = (int *)malloc(8 * sizeof(int));
  if (!buf)
    return;
  int *ptrs[2] = {buf + 1, buf + 5};
  int *nb = (int *)cat_realloc(buf, 64 * sizeof(int));
  if (nb)
    buf = nb;
  // BUG: ptrs[] still reference the previous allocation.
  *ptrs[0] = 4;
  *ptrs[1] = 5;
  cat_free(buf);
}
// SAFE FIX: store offsets instead of raw interior pointers, then recompute.

// ============================================================================
// BUG 543: cached length after shrinking a buffer
// ============================================================================
void bug543_cached_len_after_realloc(size_t n) {
  int *p = (int *)malloc(n * sizeof(int));
  if (!p)
    return;
  size_t cached_len = n;
  int *nb = (int *)cat_realloc(p, 2 * sizeof(int));
  if (nb)
    p = nb;
  // BUG: cached_len still describes the old, larger buffer.
  for (size_t i = 0; i < cached_len; ++i)
    p[i] = 0;
  cat_free(p);
}
// SAFE FIX: update cached lengths whenever the allocation size changes.

// ============================================================================
// BUG 544: retry logic frees the same block twice
// ============================================================================
void bug544_double_free_on_retry(void) {
  int *p = (int *)malloc(sizeof(int));
  if (!p)
    return;
  int attempt = 0;
  while (attempt < 2) {
    if (attempt > 0)
      cat_free(p);
    // BUG: the cleanup at the top of the second iteration frees p again.
    attempt++;
  }
  cat_free(p);
}
// SAFE FIX: free once after the retry loop and null the pointer.

int main(void) {
  return 0;
}
