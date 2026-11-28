/* Generated variant 55: allocator=3 container=1 hostile=0 seed=55 */
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
  volatile int seed = 55;
  int failures = 0;
  stdlib_datastack *ds = stdlib_thread_datastack(&e);
  stdlib_allocator A = stdlib_scratch_allocator();
  (void)ds;
  vector v = vector_in(&A);
  int k; for (k = 0; k < 16 + seed % 16; k++) if (v.push_back(k + seed) != 0) failures++;
  if (v.size() == 0 || v.at(0) != seed) failures++;
  v.drop();
  void *p = stdlib_allocate(&A, (size_t)-1, 8, &e);
  if (p != (void*)0) failures++;
  print("VARIANT", 55, failures);
  return failures == 0 ? 0 : 1;
}
