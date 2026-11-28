/*===-- test_retired_probe_linux.c - #234/#239 non-Apple stale policy -------===
 *
 * Compiled together with runtime/cplus_runtime.c under -U__APPLE__ -D__linux__
 * -D__unix__ and a stub <malloc.h>.  This executes the real non-Apple branch
 * of cplus_retired_address_stale().
 *
 * #234: the stale check must never call malloc_usable_size() on the retired
 * pointer.  The stub returns 0 for that address, which would make a stale-check
 * probe conclude "dead" and reject the registration with 105.
 *
 * #239: the missing probe must not become a blanket rejection.  Re-registering
 * the (reused) retired address must succeed, advance its generation and leave
 * the pointer live.  Only the post-acceptance clamp path may probe it.
 *
 *===----------------------------------------------------------------------===*/

#include "cplus/cplus.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int usable_probe_calls = 0;
static const void *volatile retired_probe;

size_t malloc_usable_size(void *p) {
  ++usable_probe_calls;
  /* 0 would make a stale-check probe reject the address (105); in the live
   * clamp path 0 simply keeps the caller's declared size. */
  return p == retired_probe ? 0u : 64u;
}

int main(void) {
  void *p = malloc(32);
  if (!p) {
    fprintf(stderr, "  FAIL malloc failed\n");
    return 1;
  }
  cplus_register_allocation(p, 32, __FILE__, __LINE__);
  uint64_t gen = cplus_allocation_generation(p);
  if (gen == 0u) {
    fprintf(stderr, "  FAIL registration has no generation\n");
    return 2;
  }
  cplus_release(p); /* p is now retired and freed */

  retired_probe = p;
  int before = usable_probe_calls;
  cplus_register_allocation(p, 32, __FILE__, __LINE__);

  if (cplus_allocation_generation(p) <= gen) {
    fprintf(stderr, "  FAIL re-registration did not advance generation\n");
    return 3;
  }
  if (usable_probe_calls != before + 1) {
    fprintf(stderr, "  FAIL expected one clamp probe, got %d\n",
            usable_probe_calls - before);
    return 4;
  }
  cplus_check_live(p); /* must not trap: the address is live again */
  printf("retired_probe_linux: ok\n");
  return 0;
}
