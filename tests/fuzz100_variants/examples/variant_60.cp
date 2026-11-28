/* Generated variant 60: allocator=0 container=0 hostile=0 seed=60 */
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
  volatile int seed = 60;
  int failures = 0;
  stdlib_allocator A = stdlib_heap_allocator();
  string s = string_in(&A, "variant");
  int k; for (k = 0; k < 8 + seed % 8; k++) if (s.append("x") != 0) failures++;
  if (s.size() == 0) failures++;
  s.drop();
  void *p = stdlib_allocate(&A, (size_t)-1, 8, &e);
  if (p != (void*)0) failures++;
  print("VARIANT", 60, failures);
  return failures == 0 ? 0 : 1;
}
