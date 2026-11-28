/* Generated variant 64: allocator=0 container=0 hostile=4 seed=64 */
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
  volatile int seed = 64;
  int failures = 0;
  stdlib_allocator A = stdlib_heap_allocator();
  string s = string_in(&A, "variant");
  int k; for (k = 0; k < 8 + seed % 8; k++) if (s.append("x") != 0) failures++;
  if (s.size() == 0) failures++;
  s.drop();
  stdlib_scratch_mark m = stdlib_scratch_enter(stdlib_thread_datastack(&e));
  void *t = stdlib_allocate(&A, 128, 16, &e);
  if (t != (void*)0 && (((unsigned long)t)&15ul)!=0) failures++;
  stdlib_scratch_leave(stdlib_thread_datastack(&e), m);
  print("VARIANT", 64, failures);
  return failures == 0 ? 0 : 1;
}
