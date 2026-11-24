/* Generated variant 59: allocator=3 container=1 hostile=4 seed=59 */
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
  volatile int seed = 59;
  int failures = 0;
  stdlib_datastack *ds = stdlib_thread_datastack(&e);
  stdlib_allocator A = stdlib_scratch_allocator();
  (void)ds;
  vector v = vector_in(&A);
  int k; for (k = 0; k < 16 + seed % 16; k++) if (v.push_back(k + seed) != 0) failures++;
  if (v.size() == 0 || v.at(0) != seed) failures++;
  v.drop();
  stdlib_scratch_mark m = stdlib_scratch_enter(stdlib_thread_datastack(&e));
  void *t = stdlib_allocate(&A, 128, 16, &e);
  if (t != (void*)0 && (((unsigned long)t)&15ul)!=0) failures++;
  stdlib_scratch_leave(stdlib_thread_datastack(&e), m);
  print("VARIANT", 59, failures);
  return failures == 0 ? 0 : 1;
}
