/*===-- test_retired_reuse_linux.c - #239 non-Apple address reuse ------------===
 *
 * Compiled together with runtime/cplus_runtime.c through its Linux
 * preprocessor path.  A one-slot malloc/free stub makes the reuse described in
 * issue #239 deterministic: the malloc immediately after a free of the same
 * size class returns the exact address the runtime just retired.  The old
 * conservative stale check aborted that re-registration with exit 105.
 *
 * The stubbed malloc_usable_size() returns 0 for the retired address, so a
 * stale-check probe (#234) would reject it; only the post-acceptance clamp
 * path is allowed to probe it.
 *
 *===----------------------------------------------------------------------===*/

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Only the runtime's allocator calls are redirected; libc keeps the real
 * malloc/free, so stdio and the pthread setup are unaffected. */
static union {
  void *align;
  long double wide;
  unsigned char bytes[64];
} reuse_storage;
static void *reuse_slot;
static int slot_busy;
static const void *volatile retired_probe;
static int usable_probe_calls;

static void *reuse_malloc(size_t n) {
  if (n == 64u && !slot_busy) {
    slot_busy = 1;
    return reuse_slot;
  }
  return malloc(n);
}

static void reuse_free(void *p) {
  if (p == reuse_slot) {
    slot_busy = 0;
    return;
  }
  free(p);
}

#define malloc reuse_malloc
#define free reuse_free
#include "runtime/cplus_runtime.c"

size_t malloc_usable_size(void *p) {
  ++usable_probe_calls;
  return p == retired_probe ? 0u : 64u;
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
  reuse_slot = reuse_storage.bytes;

  /* A rewriter-managed malloc(): register the fresh block. */
  void *a = malloc(64);
  if (a != reuse_slot) return 1;
  cplus_register_allocation(a, 64, __FILE__, __LINE__);
  uint64_t gen_a = cplus_allocation_generation(a);
  if (gen_a == 0u) return 2;

  /* A rewriter-managed free(): retire the block and release the slot. */
  cplus_free_registered(a, __FILE__, __LINE__);
  if (slot_busy) return 3;

  /* The next malloc() of the same size class reuses the retired address. */
  void *b = malloc(64);
  if (b != a) return 4;
  retired_probe = b;
  int before = usable_probe_calls;

  if (setjmp(trap) == 0) {
    cplus_register_allocation(b, 64, __FILE__, __LINE__);
    if (got_code != 0) return 5;
    if (usable_probe_calls != before + 1) return 6;
    if (cplus_allocation_generation(b) <= gen_a) return 7;
    cplus_check_live(b);
    if (!slot_busy) return 8;
    cplus_free_registered(b, __FILE__, __LINE__);
    if (slot_busy) return 9;
  } else {
    fprintf(stderr, "  FAIL register reused address: fatal %d\n",
            (int)got_code);
    return 10;
  }

  /* The realloc transfer path has the same stale-address guard. */
  void *c = malloc(64);
  if (c != reuse_slot) return 11;
  cplus_register_allocation(c, 64, __FILE__, __LINE__);
  cplus_free_registered(c, __FILE__, __LINE__);
  void *d = malloc(64);
  if (d != c) return 12;
  retired_probe = d;
  before = usable_probe_calls;

  if (setjmp(trap) == 0) {
    cplus_realloc_registered(NULL, d, 64, __FILE__, __LINE__);
    if (got_code != 0) return 13;
    if (usable_probe_calls != before + 1) return 14;
    if (cplus_allocation_generation(d) <= gen_a) return 15;
    cplus_check_live(d);
    if (!slot_busy) return 16;
    cplus_free_registered(d, __FILE__, __LINE__);
    if (slot_busy) return 17;
  } else {
    fprintf(stderr, "  FAIL realloc reused address: fatal %d\n",
            (int)got_code);
    return 18;
  }

  puts("retired_reuse_linux: ok");
  return 0;
}
