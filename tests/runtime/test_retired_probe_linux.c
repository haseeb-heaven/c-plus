/*===-- test_retired_probe_linux.c - #234 non-Apple stale policy -------------===
 *
 * Compiled together with runtime/cplus_runtime.c under -U__APPLE__ -D__linux__
 * -D__unix__ and a stub <malloc.h>.  This executes the real non-Apple branch
 * of cplus_retired_address_stale(): a retired pointer must be rejected before
 * any usable-size probe dereferences its (already freed) chunk header.
 *
 * The stub counts probes, so the test fails both if the stale address is
 * re-registered and if the broken malloc_usable_size() probe is called during
 * the stale check.  cplus_realloc() keeps using the probe on live pointers,
 * which is why the initial registration is allowed to make one call.
 *
 *===----------------------------------------------------------------------===*/

#include "cplus/cplus.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static int usable_probe_calls = 0;

size_t malloc_usable_size(void *p) {
  (void)p;
  ++usable_probe_calls;
  return 64u; /* a stale glibc chunk also reports nonzero: never proof of life */
}

static jmp_buf trap;
static cplus_exit_code got_code = (cplus_exit_code)0;

static void hook(cplus_exit_code code, const char *kind) {
  (void)kind;
  got_code = code;
  longjmp(trap, 1);
}

int main(void) {
  cplus_fatal_hook = hook;
  void *p = malloc(32);
  if (!p) {
    fprintf(stderr, "  FAIL malloc failed\n");
    return 1;
  }
  cplus_register_allocation(p, 32, __FILE__, __LINE__);
  cplus_release(p); /* p is now freed and present in the retired ledger */

  int before = usable_probe_calls;
  if (setjmp(trap) == 0) {
    cplus_register_allocation(p, 32, __FILE__, __LINE__);
    fprintf(stderr, "  FAIL stale retired address was re-registered\n");
    return 1;
  }
  if (got_code != CPLUS_EXIT_TYPE_CONFUSION) {
    fprintf(stderr, "  FAIL wrong fatal code %d (expected %d)\n",
            (int)got_code, (int)CPLUS_EXIT_TYPE_CONFUSION);
    return 1;
  }
  if (usable_probe_calls != before) {
    fprintf(stderr, "  FAIL usable-size probe called on a retired pointer\n");
    return 1;
  }
  printf("retired_probe_linux: ok\n");
  return 0;
}
