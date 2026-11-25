#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Teaching catalog: allocator calls are indirected through function pointers
   so that these intentionally-broken examples still build under C+ default
   checks.  Each BUG comment describes the real-world hazard. */
static void *(*cat_realloc)(void *, size_t) = realloc;
static void (*cat_free)(void *) = free;

// ============================================================================
// BUG 505: realloc failure losing original pointer (NULL then dereferenced)
// ============================================================================
void bug505_realloc_failure_null_deref(void) {
  int *p = (int *)malloc(4 * sizeof(int));
  if (!p)
    return;
  p = (int *)cat_realloc(p, 1024 * sizeof(int));
  // BUG: on failure realloc returns NULL; the original block is leaked and
  // p[0] dereferences NULL.
  p[0] = 7;
  cat_free(p);
}
// SAFE FIX: assign to a temporary, test it for NULL, and keep/free the
// original block when reallocation fails.

// ============================================================================
// BUG 506: realloc of an interior pointer (not the start of a malloc block)
// ============================================================================
void bug506_realloc_interior_pointer(void) {
  int *buf = (int *)malloc(8 * sizeof(int));
  if (!buf)
    return;
  int *mid = buf + 3;
  int *res = (int *)cat_realloc(mid, 16 * sizeof(int));
  // BUG: mid does not point to the start of an allocated block; realloc will
  // pass an interior address to the allocator and corrupt the heap.
  cat_free(res);
}
// SAFE FIX: only realloc a pointer that was returned by malloc/calloc/realloc.

// ============================================================================
// BUG 507: realloc of static storage
// ============================================================================
static int bug507_static_buf[16];
void bug507_realloc_static_storage(void) {
  int *res = (int *)cat_realloc(bug507_static_buf, 64 * sizeof(int));
  // BUG: static arrays live in .data/.bss, not the heap; realloc cannot grow
  // them and calls the deallocator on a non-heap address.
  cat_free(res);
}
// SAFE FIX: allocate a heap copy and copy the static data into it.

// ============================================================================
// BUG 508: realloc of automatic (stack) storage
// ============================================================================
void bug508_realloc_stack_storage(void) {
  int local[16];
  int *res = (int *)cat_realloc(local, 64 * sizeof(int));
  // BUG: local lives on the stack; realloc hands a stack address to free().
  cat_free(res);
}
// SAFE FIX: heap-allocate the buffer from the start if it must be resized.

// ============================================================================
// BUG 509: realloc of a pointer to read-only (const) storage
// ============================================================================
void bug509_realloc_const_alias(void) {
  const char *lit = "constant text";
  void *res = cat_realloc((void *)lit, 64);
  // BUG: string literals are read-only static data; resizing/freeing them is
  // undefined behavior.
  cat_free(res);
}
// SAFE FIX: copy the literal into a mutable heap buffer before resizing.

// ============================================================================
// BUG 510: freeing the old pointer after a successful realloc
// ============================================================================
void bug510_realloc_success_then_free_old(void) {
  int *p = (int *)malloc(8 * sizeof(int));
  if (!p)
    return;
  int *q = (int *)cat_realloc(p, 32 * sizeof(int));
  if (q) {
    cat_free(p);
    // BUG: realloc already released p on success; this is a double free.
    p = q;
  }
  cat_free(p);
}
// SAFE FIX: on success the old pointer is already invalid; never free it.

// ============================================================================
// BUG 511: cached end pointer used after shrinking a buffer
// ============================================================================
void bug511_cached_end_after_shrink(void) {
  size_t n = 16;
  int *p = (int *)malloc(n * sizeof(int));
  if (!p)
    return;
  int *end = p + n;
  int *q = (int *)cat_realloc(p, 4 * sizeof(int));
  if (q)
    p = q;
  // BUG: end still points one past the original (larger) buffer; after the
  // shrink it is far past the end of the live allocation.
  *(end - 1) = 9;
  cat_free(p);
}
// SAFE FIX: recompute end from the new base and new length after realloc.

// ============================================================================
// BUG 512: cached interior pointer used after growing a buffer
// ============================================================================
void bug512_cached_mid_after_grow(void) {
  int *p = (int *)malloc(4 * sizeof(int));
  if (!p)
    return;
  int *mid = p + 2;
  int *q = (int *)cat_realloc(p, 64 * sizeof(int));
  if (q)
    p = q;
  // BUG: if realloc moved the block, mid now points into freed storage.
  *mid = 3;
  cat_free(p);
}
// SAFE FIX: recompute all interior pointers from the new base after realloc.

// ============================================================================
// BUG 513: allocation size underflow from header subtraction
// ============================================================================
void bug513_realloc_size_underflow(void) {
  size_t header = 32;
  size_t total = 16;
  size_t payload = total - header;
  // BUG: 16 - 32 wraps to a huge size_t; realloc fails and the state is
  // inconsistent.
  int *p = (int *)malloc(total);
  if (!p)
    return;
  int *q = (int *)cat_realloc(p, payload);
  if (q)
    p = q;
  cat_free(p);
}
// SAFE FIX: check total >= header before subtracting, and use checked math.

// ============================================================================
// BUG 514: realloc size given as element count instead of byte count
// ============================================================================
void bug514_realloc_size_mismatch(void) {
  size_t count = 8;
  int *p = (int *)malloc(count * sizeof(int));
  if (!p)
    return;
  int *q = (int *)cat_realloc(p, count);
  // BUG: count is elements, but realloc wants bytes; the buffer shrinks to 8
  // bytes and the following indexed write overflows it.
  if (q)
    p = q;
  p[7] = 1;
  cat_free(p);
}
// SAFE FIX: multiply the element count by sizeof(*p) before calling realloc.

int main(void) {
  return 0;
}
