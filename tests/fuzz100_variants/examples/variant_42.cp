/* Generated variant 42: allocator=2 container=0 hostile=2 seed=42 */
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
  volatile int seed = 42;
  int failures = 0;
  stdlib_mem_pool pool; stdlib_mem_pool_init(&pool, 1024, 8, &e);
  stdlib_allocator A = stdlib_mem_pool_as_allocator(&pool);
  string s = string_in(&A, "variant");
  int k; for (k = 0; k < 8 + seed % 8; k++) if (s.append("x") != 0) failures++;
  if (s.size() == 0) failures++;
  s.drop();
  void *r = stdlib_allocate(&A, 64, 3, &e); /* invalid alignment */
  if (r != (void*)0) failures++;
  stdlib_mem_pool_destroy(&pool, &e);
  print("VARIANT", 42, failures);
  return failures == 0 ? 0 : 1;
}
