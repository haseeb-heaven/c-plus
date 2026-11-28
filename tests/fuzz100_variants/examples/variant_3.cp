/* Generated variant 3: allocator=3 container=1 hostile=3 seed=3 */
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
  volatile int seed = 3;
  int failures = 0;
  stdlib_datastack *ds = stdlib_thread_datastack(&e);
  stdlib_allocator A = stdlib_scratch_allocator();
  (void)ds;
  vector v = vector_in(&A);
  int k; for (k = 0; k < 16 + seed % 16; k++) if (v.push_back(k + seed) != 0) failures++;
  if (v.size() == 0 || v.at(0) != seed) failures++;
  v.drop();
  unsigned char stackbuf[32];
  stdlib_slot_ref sr; sr.pool=(stdlib_slot_pool*)0; sr.index=0; sr.generation=0;
  if (stdlib_slot_ref_get(&sr, &e) != (void*)0) failures++;
  (void)stackbuf;
  print("VARIANT", 3, failures);
  return failures == 0 ? 0 : 1;
}
