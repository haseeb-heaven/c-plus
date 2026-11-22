/* bugs4.cp - C++-style lifetime and object-model mistakes translated to C/C+.
 *
 * IDs 305-314: dangling references to locals, compound-literal escapes and
 *              pointers whose storage dies before their uses.
 *
 * These are compile-time negative fixtures.  main() deliberately does NOT
 * call any buggy function: the catalog documents the pattern, it is not a
 * runnable crash demo.
 */
#define _POSIX_C_SOURCE 200809L
#include <alloca.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int *g_dangling_slot;

// BUG 305: Returning a pointer to a local automatic variable; the frame is
// popped before the caller can dereference the result.
// SAFE FIX: Return the value, or write into caller-owned storage / use a
// managed allocation that outlives the call.
int *bug305_return_address_of_local(void) {
  int value = 42;
  return &value;
}

// BUG 306: Publishing the address of a local into a longer-lived global; the
// pointer is a dangling reference as soon as the function returns.
// SAFE FIX: Copy the value into the global or allocate it with a lifetime that
// matches the global's lifetime.
void bug306_publish_local_to_global(void) {
  char label[16] = "temporary";
  g_dangling_slot = (int *)label;
  (void)g_dangling_slot;
}

// BUG 307: A compound literal has automatic lifetime in the enclosing block;
// returning &(int){...} leaves the caller with a pointer to dead storage.
// SAFE FIX: Return by value, or heap-allocate with a matching destructor.
int *bug307_return_compound_literal_address(void) {
  return &(int){7};
}

// BUG 308: Taking a pointer to an element of a block-scoped array and storing
// it in caller-provided out-param; the array dies at block exit.
// SAFE FIX: Have the caller supply the buffer, or copy the element out.
void bug308_escape_block_array_element(int **out) {
  {
    int coords[3] = {1, 2, 3};
    *out = &coords[1];
  }
}

// BUG 309: A pointer to a field of a local struct escapes through a static
// cache; the containing object is gone on return.
// SAFE FIX: Copy the field value or store an owning handle instead of a
// pointer into a dead object.
static int *g_field_cache;
void bug309_cache_local_struct_field(void) {
  struct Point {
    int x, y;
  } p = {10, 20};
  g_field_cache = &p.x;
}

// BUG 310: Returning a pointer into a local variable-length array; the VLA's
// storage is reclaimed with the frame, and its size is unknown to the caller.
// SAFE FIX: Allocate with cplus_alloc/cplus_new and return an owning pointer.
int *bug310_return_vla_element(size_t n) {
  int buf[n];
  buf[0] = n;
  return &buf[0];
}

// BUG 311: alloca storage handed to the caller through an out-param; the stack
// memory is invalid after the allocating function returns.
// SAFE FIX: Use a managed allocation whose lifetime is controlled by the
// owner, never stack memory that escapes the frame.
void bug311_alloca_escape_out_param(size_t n, int **out) {
  int *tmp = (int *)alloca(n * sizeof(int));
  tmp[0] = 1;
  *out = tmp;
}

// BUG 312: A pointer to a local declared in a nested scope is remembered by a
// static pointer and dereferenced later; the nested frame no longer exists.
// SAFE FIX: Store a copy, not a pointer into a short-lived scope.
static int *g_nested_ptr;
void bug312_nested_scope_pointer_escape(void) {
  g_nested_ptr = &(int){99};
}

// BUG 313: Returning a pointer to a member of a local union; union storage is
// automatic and its active member is gone on return.
// SAFE FIX: Return the scalar member by value.
int *bug313_return_local_union_member(void) {
  union Value {
    int i;
    float f;
  } v;
  v.i = 5;
  return &v.i;
}

// BUG 314: Returning a pointer produced by a compound literal inside a chained
// call; the temporary dies at the end of the full expression.
// SAFE FIX: Materialize the value in caller-owned storage before returning.
int *bug314_chained_temporary_escape(void) {
  int *const p = (int[]){11, 22, 33};
  return p + 1;
}

int main(void) { return 0; }
