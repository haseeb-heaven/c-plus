/* bugs7.cp - initializer temporaries, sizeof confusion and destruction order.
 *
 * IDs 335-344: size computations on the wrong object, pointers to initializer
 *              temporaries, and cleanup callbacks run in the wrong order.
 *
 * Compile-time negative fixtures: main() does not call the buggy functions.
 */
#define _POSIX_C_SOURCE 200809L
#include "cplus/cplus.h"
#include <alloca.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// BUG 335: sizeof applied to a pointer to a managed allocation gives the
// pointer size, not the size of the managed object it points to.
// SAFE FIX: Use cplus_alloc_bytes() / countof() on the managed allocation, or
// carry the element count alongside the pointer.
size_t bug335_sizeof_managed_pointer(void) {
  int *managed_buf = (int *)cplus_alloc(8 * sizeof(int));
  return sizeof(managed_buf);
}

// BUG 336: sizeof on a decayed array parameter yields the pointer size, so the
// computed element count is wrong and buffers are under/over-cleared.
// SAFE FIX: Pass the element count explicitly or use countof on a real array.
void bug336_sizeof_decayed_parameter(int data[10]) {
  size_t n = sizeof(data) / sizeof(data[0]);
  memset(data, 0, n * sizeof(int));
}

// BUG 337: A pointer into an initializer-list-style compound-literal array is
// stored in a struct that outlives the enclosing full expression.
// SAFE FIX: Copy the initializer elements into owned storage.
struct View {
  const int *items;
  size_t count;
};
struct View bug337_initializer_temporary_escape(void) {
  struct View v = {(const int[]){1, 2, 3}, 3};
  return v;
}

// BUG 338: A compound literal in a nested initializer is remembered as a
// pointer; its automatic lifetime ends at the closing brace.
// SAFE FIX: Store the value by copy, not a pointer to a temporary.
static const char *g_label;
void bug338_compound_literal_in_initializer(void) {
  struct Holder {
    const char *name;
  } h = {(const char[]){'a', 'b', 0}};
  g_label = h.name;
}

// BUG 339: Destructor ordering bug: the child resource is released before the
// parent that still holds and uses a pointer to it.
// SAFE FIX: Destroy dependents in reverse order of construction (LIFO).
void bug339_destructor_order_child_first(void) {
  int *child = (int *)malloc(sizeof(int));
  int **parent = (int **)malloc(sizeof(int *));
  *parent = child;
  free(child);
  printf("%d\n", **parent);
  free(parent);
}

// BUG 340: A managed pointer escapes its owning scope, so the runtime releases
// the object while the caller still holds the pointer.
// SAFE FIX: Copy the data out or use an explicit resource with ownership
// transfer instead of returning a managed pointer.
int *bug340_managed_escape(void) {
  managed int *m = cplus_new(int);
  *m = 12;
  return m;
}

// BUG 341: sizeof a VLA counts bytes correctly but evaluates the size operand
// at run time, so relying on it as a compile-time extent is a lifetime bug.
// SAFE FIX: Use a fixed extent or explicit runtime length with managed memory.
void bug341_sizeof_vla_as_compile_time_extent(size_t n) {
  int buf[n];
  enum {
    CAP = sizeof buf
  };
  (void)CAP;
}

// BUG 342: A chained initializer uses a temporary string buffer whose storage
// dies at the end of the expression, leaving a dangling pointer in the result.
// SAFE FIX: Copy the string into owned storage with strdup/managed allocation.
const char *bug342_chained_initializer_temporary(void) {
  char full[32];
  snprintf(full, sizeof full, "%s-%s", "first", "last");
  return full;
}

// BUG 343: Cleanup records are registered in the wrong order, so a resource
// that depends on another is destroyed first.
// SAFE FIX: Register cleanups so that their execution order is reverse of
// acquisition.
void bug343_cleanup_registration_order(void) {
  int *dependency = (int *)malloc(sizeof(int));
  int *owner = (int *)malloc(sizeof(int));
  *owner = *dependency;
  free(dependency);
  free(owner);
}

// BUG 344: alloca is used to store data that is treated as an owned object and
// released later; the pointer is stack memory, not managed memory.
// SAFE FIX: Use cplus_alloc/cplus_new for objects that need managed lifetime.
void bug344_alloca_as_managed_object(size_t n) {
  int *obj = (int *)alloca(n * sizeof(int));
  obj[0] = 13;
  printf("%d\n", obj[0]);
}

int main(void) { return 0; }
