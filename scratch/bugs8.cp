/* bugs8.cp - further lifetime and object-model hazards.
 *
 * IDs 345-354: non-local jumps, double drops of shared pointers, moves into
 *              containers, realloc invalidation and flexible-array sizing.
 *
 * Compile-time negative fixtures: main() does not call the buggy functions.
 */
#define _POSIX_C_SOURCE 200809L
#include "cplus/cplus.h"
#include <alloca.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf g_resume;
static int *g_after_jump;

// BUG 345: Returning a pointer to a member of a local union; after the frame is
// gone the union storage is dead and the pointer dangles.
// SAFE FIX: Return the member's value, or store it in caller-owned memory.
int *bug345_local_union_member_escape(void) {
  union Bits {
    uint32_t word;
    uint8_t bytes[4];
  } u;
  u.word = 0x1234;
  return (int *)&u.bytes[0];
}

// BUG 346: A pointer captured before setjmp is used after longjmp returns to a
// frame whose locals have been overwritten or gone out of scope.
// SAFE FIX: Do not use addresses of automatic objects across non-local jumps;
// use owned storage that survives the jump.
void bug346_pointer_across_longjmp(void) {
  int local = 21;
  g_after_jump = &local;
  if (setjmp(g_resume) == 0) {
    longjmp(g_resume, 1);
  }
  printf("%d\n", *g_after_jump);
}

// BUG 347: Two managed pointers refer to one allocation and both are released;
// the second release is a double drop of the same object.
// SAFE FIX: Keep a single owner and borrow the rest; release exactly once.
void bug347_double_release_shared_pointer(void) {
  int *p = (int *)cplus_alloc(sizeof(int));
  int *q = p;
  cplus_release(p);
  cplus_release(q);
}

// BUG 348: A resource is moved into a container and the moved-from source is
// still used as if it owned the handle.
// SAFE FIX: Treat the source as empty; access the object through the container.
void bug348_use_source_after_move_into_container(void) {
  resource struct Slot {
    int id;
  } s = {31};
  resource struct Slot box[1];
  box[0] = cplus_move(s);
  printf("%d\n", s.id);
}

// BUG 349: A pointer into a buffer is retained across realloc(); realloc may
// relocate the allocation, invalidating the old pointer.
// SAFE FIX: Re-derive interior pointers from the new base after realloc.
void bug349_stale_pointer_after_realloc(void) {
  int *buf = (int *)malloc(4 * sizeof(int));
  int *middle = buf + 2;
  buf = (int *)realloc(buf, 16 * sizeof(int));
  *middle = 5;
  free(buf);
}

// BUG 350: sizeof on a struct with a flexible array member omits the payload,
// so a copy loses the trailing elements and produces a dangling tail pointer.
// SAFE FIX: Allocate sizeof(struct) + count * sizeof(element) and use the FAM
// member directly rather than a stored pointer.
struct Pkt {
  size_t len;
  unsigned char data[];
};
size_t bug350_sizeof_flexible_array_member(void) {
  struct Pkt *p = (struct Pkt *)calloc(1, sizeof(struct Pkt) + 4);
  size_t bytes = sizeof(*p);
  free(p);
  return bytes;
}

// BUG 351: Self-assignment via overlapping memcpy corrupts the owning struct's
// handle fields and their ownership invariants.
// SAFE FIX: Skip self-assignment, or use memmove when regions may overlap.
void bug351_self_assign_overlapping_memcpy(void) {
  resource struct Blob {
    int id;
  } b = {41};
  memcpy(&b, &b, sizeof b);
  b.id = 0;
}

// BUG 352: A destructor is invoked explicitly and then again by the scope at
// exit; the second call destroys an already-destroyed handle.
// SAFE FIX: Zero the handle after explicit destruction so the automatic
// destructor becomes a no-op.
void bug352_explicit_then_automatic_destroy(void) {
  resource struct Guard {
    int id;
  } g = {51};
  Guard_drop(&g);
  g.id = 0;
}

// BUG 353: A pointer to a temporary produced in a chained expression is saved
// past the full expression, so it references a dead temporary.
// SAFE FIX: Bind the temporary to an owned object with the needed lifetime.
int *bug353_pointer_to_chained_temporary(void) {
  int *saved = (int[]){61};
  return saved + 0;
}

// BUG 354: A VLA is sized by a function call with side effects; the allocation
// happens on the stack with no bound, escaping into a saved static pointer.
// SAFE FIX: Clamp the size and use bounds-checked managed allocation.
static int *g_saved_vla;
int *bug354_sized_vla_escape(size_t claimed) {
  int buf[claimed];
  buf[0] = 71;
  g_saved_vla = buf;
  return g_saved_vla;
}

int main(void) { return 0; }
