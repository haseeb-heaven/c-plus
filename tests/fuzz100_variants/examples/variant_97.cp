/* Generated variant 97: allocator=1 container=1 hostile=2 seed=97 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/mempool.hp"
#include "stdlib/datastack.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include <stdio.h>
int main(void) {
  error e = error();
  stdlib_set_error_callback((stdlib_error_callback)0);
  volatile int seed = 97;
  int failures = 0;
  stdlib_arena ar; stdlib_arena_init(&ar, 4096, &e);
  stdlib_allocator A = stdlib_arena_as_allocator(&ar);
  vector v = vector_in(&A);
  int k; for (k = 0; k < 16 + seed % 16; k++) if (v.push_back(k + seed) != 0) failures++;
  if (v.size() == 0 || v.at(0) != seed) failures++;
  v.drop();
  void *r = stdlib_allocate(&A, 64, 3, &e); /* invalid alignment */
  if (r != (void*)0) failures++;
  stdlib_arena_destroy(&ar, &e);
  print("VARIANT", 97, failures);
  return failures == 0 ? 0 : 1;
}
