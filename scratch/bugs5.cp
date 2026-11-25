/* bugs5.cp - move-from-then-use, double destruction and self-assignment.
 *
 * IDs 315-324: resource handles that are copied, moved from, aliased or
 *              self-assigned so that ownership is violated.
 *
 * Compile-time negative fixtures: main() does not call the buggy functions.
 */
#define _POSIX_C_SOURCE 200809L
#include "cplus/cplus.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

resource struct Ticket {
  int id;
};
void Ticket_drop(Ticket *t) {
  if (t->id)
    t->id = 0;
}

// BUG 315: Reading a resource after cplus_move() zeroed it; the moved-from
// handle no longer owns anything.
// SAFE FIX: Re-initialize the source before use, or use the returned owner.
void bug315_read_moved_from_resource(void) {
  Ticket a = {7};
  Ticket b = cplus_move(a);
  printf("%d\n", a.id);
  (void)b;
}

// BUG 316: Two structs share one heap handle; each destructor frees it, giving
// double destruction instead of a single owner.
// SAFE FIX: Transfer ownership with cplus_move so only one object owns it.
void bug316_double_destroy_via_shallow_copy(void) {
  int *shared = (int *)malloc(sizeof(int));
  int *copy = shared;
  free(shared);
  free(copy);
}

// BUG 317: Self-assignment of an owning struct overwrites the handle before
// the old value can be released, corrupting ownership.
// SAFE FIX: Guard with `if (self != other)` or implement a safe move-assign.
void bug317_self_assignment_loses_owner(void) {
  Ticket t = {5};
  Ticket *alias = &t;
  t = *alias;
  Ticket_drop(&t);
}

// BUG 318: Several handles are constructed from one raw allocation; every
// handle's destructor releases the same pointer.
// SAFE FIX: Give each owner its own allocation, or use one owning handle and
// borrow the rest.
void bug318_aliased_owners_double_drop(void) {
  int *buf = (int *)calloc(4, sizeof(int));
  int *alias_a = buf;
  int *alias_b = buf;
  free(alias_a);
  free(alias_b);
}

// BUG 319: A borrow of a resource is kept after the owner is moved elsewhere;
// the borrow points at a zeroed, moved-from handle.
// SAFE FIX: Re-borrow from the new owner after a move.
void bug319_borrow_after_owner_moved(void) {
  Ticket a = {9};
  Ticket *borrowed = &a;
  Ticket b = cplus_move(a);
  printf("%d\n", borrowed->id);
  (void)b;
}

// BUG 320: A hand-written swap copies a resource through a temporary, so both
// objects briefly own the same handle and destructors run twice.
// SAFE FIX: Use cplus_move for both halves of the swap.
void bug320_swap_by_copy_double_free(void) {
  Ticket a = {1}, b = {2};
  Ticket tmp = a;
  a = b;
  b = tmp;
  Ticket_drop(&a);
  Ticket_drop(&b);
}

// BUG 321: A VLA of resource handles is copied wholesale into another array;
// copied owners all reference the same underlying resources.
// SAFE FIX: Allocate an owning managed array and move each element.
void bug321_vla_handle_array_copy(size_t n) {
  Ticket owners[n];
  Ticket copies[n];
  owners[0].id = 1;
  memcpy(copies, owners, n * sizeof(Ticket));
}

// BUG 322: A destructor is invoked explicitly and again automatically at scope
// exit, so the same handle is destroyed twice.
// SAFE FIX: Let the scope's destructor run exactly once, or zero the handle
// after an explicit destroy.
void bug322_explicit_and_automatic_drop(void) {
  Ticket t = {4};
  Ticket_drop(&t);
  Ticket_drop(&t);
}

// BUG 323: alloca storage holding handles escapes the frame; the destructors
// of the escaped handles run after the backing stack memory is gone.
// SAFE FIX: Use managed allocations for data that outlives the frame.
void bug323_alloca_handle_escape(size_t n) {
  Ticket *frame_handles = (Ticket *)alloca(n * sizeof(Ticket));
  frame_handles[0].id = 3;
  Ticket_drop(&frame_handles[0]);
}

// BUG 324: Self-assignment implemented with overlapping memcpy destroys the
// object's own bytes (and any ownership invariant they encode).
// SAFE FIX: Skip self-assignment or use memmove/assignment guards.
void bug324_self_memcpy_overlap(void) {
  Ticket t = {8};
  memcpy(&t, &t, sizeof t);
}

int main(void) { return 0; }
