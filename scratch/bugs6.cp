/* bugs6.cp - stack-storage lifetime escape: VLA and alloca mistakes.
 *
 * IDs 325-334: variable-length arrays and alloca buffers whose storage is
 *              captured, returned, resized or sized from untrusted input.
 *
 * Compile-time negative fixtures: main() does not call the buggy functions.
 */
#define _POSIX_C_SOURCE 200809L
#include <alloca.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int *g_saved_vla;
static void *g_saved_alloca;

// BUG 325: Returning the address of a VLA element; the VLA dies with the
// declaring frame, so the returned pointer dangles immediately.
// SAFE FIX: Allocate on the heap with a lifetime that matches the caller.
int *bug325_return_vla_address(size_t n) {
  int row[n];
  row[0] = 1;
  return row;
}

// BUG 326: alloca buffer stored in a static slot and used by later calls; the
// stack region is reused and no longer holds the original data.
// SAFE FIX: Use a managed allocation that persists across calls.
void bug326_alloca_into_static_slot(size_t n) {
  int *buf = (int *)alloca(n * sizeof(int));
  buf[0] = 2;
  g_saved_alloca = buf;
}

// BUG 327: A pointer to a VLA is stored inside a struct that is returned to the
// caller, so the struct outlives the array it points into.
// SAFE FIX: Store an owning managed pointer or copy the array contents.
struct Slice {
  int *data;
  size_t len;
};
struct Slice bug327_vla_in_returned_struct(size_t n) {
  int tmp[n];
  tmp[0] = 3;
  struct Slice s = {tmp, n};
  return s;
}

// BUG 328: Using sizeof on a VLA evaluates the size expression at run time, so
// an incrementing index has an observable side effect inside sizeof.
// SAFE FIX: Compute extents explicitly and use a fixed-size type or cplus
// managed allocation.
void bug328_sizeof_vla_side_effect(int n) {
  int a[n++];
  size_t bytes = sizeof a;
  printf("%zu %d\n", bytes, n);
}

// BUG 329: A VLA is declared inside a loop and its address saved for later
// iterations; each iteration reuses the same stack slot, clobbering saved data.
// SAFE FIX: Allocate once with a lifetime spanning all iterations.
int *bug329_vla_saved_across_iterations(size_t n) {
  int *last = NULL;
  for (size_t i = 0; i < 3; ++i) {
    int buf[n];
    buf[0] = (int)i;
    last = buf;
  }
  return last;
}

// BUG 330: alloca called repeatedly in a loop grows the stack without bound;
// alloca memory is only released when the function returns.
// SAFE FIX: Allocate a single buffer sized once, or use heap storage.
void bug330_alloca_in_growing_loop(size_t n) {
  for (size_t i = 0; i < 100; ++i) {
    int *chunk = (int *)alloca(n * sizeof(int));
    chunk[0] = (int)i;
  }
}

// BUG 331: A VLA is passed to a helper that caches the pointer globally; the
// helper cannot know the callee-owned array's short lifetime.
// SAFE FIX: Pass an owned copy or a managed allocation to the helper.
void bug331_cache_vla_in_helper(int n, int **sink) {
  int buf[n];
  buf[0] = 4;
  *sink = buf;
  g_saved_vla = buf;
}

// BUG 332: A compound literal whose bound is a VLA is stored as a pointer; the
// temporary compound literal does not live past the enclosing expression.
// SAFE FIX: Copy the elements into storage owned by the destination.
int (*bug332_vla_bound_compound_literal(size_t n))[] {
  int (*p)[n] = &(int[n]){5};
  return p;
}

// BUG 333: alloca storage is written through an out-param, but the caller
// treats it as heap storage and releases it later.
// SAFE FIX: Never free stack storage; allocate with a managed allocator.
void bug333_alloca_out_param_freed_later(size_t n, int **out) {
  int *scratch = (int *)alloca(n * sizeof(int));
  scratch[0] = 6;
  *out = scratch;
}

// BUG 334: A VLA is sized directly from untrusted input, allowing an attacker
// to force unbounded stack consumption (stack overflow).
// SAFE FIX: Clamp the size and use checked heap allocation (cplus_alloc_array).
void bug334_untrusted_vla_size(size_t claimed) {
  int big[claimed];
  big[0] = 7;
  printf("%d\n", big[0]);
}

int main(void) { return 0; }
