/* Linux: pthread_getattr_np() in the included runtime needs GNU feature
 * macros declared before the first libc header in this TU as well. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

/* Deterministic fork-based regression for issue #184.
 *
 * Headline criterion: with opt-in epoch/conservative reclamation, a retired
 * address is NOT reused while a pointer to it is still reachable.  The test
 * includes the real runtime/cplus_runtime.c (same pattern as
 * test_shadow_metadata.c), so the collector and the side table are the live
 * ones.  No project build, game assets or external services are involved.
 *
 * run.sh invokes the same binary twice:
 *   CPLUS_RECLAIM_MODE=epoch ./test_reclaim gc-on   (opt-in path)
 *                            ./test_reclaim gc-off  (unchanged default)
 *
 * Covered:
 *   - env opt-in sampled once; default stays immediate-free;
 *   - a block retired while a stack word and a registered global word still
 *     point at it survives multiple epoch boundaries;
 *   - neither cplus_alloc nor plain malloc can hand out that address while
 *     the root is live, and the original payload is still intact;
 *   - a second release of the retained block still traps with 104;
 *   - unreferenced blocks are eventually reclaimed (progress, not a leak);
 *   - plain-pointer ABI and sizeof/offsetof are unchanged by the opt-in.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "cplus/cplus.h"

/* Pull in the implementation so the collector and side table are real. */
#include "../../../runtime/cplus_runtime.c"

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      return 1;                                                                \
    }                                                                          \
  } while (0)

#define MASK 0xa5a5a5a5a5a5a5a5ULL

/* A global pointer word.  The collector scans global words and also honours
 * explicit slot registration; both keep the retired block alive. */
static void *g_global_root;
/* Address stored XOR-masked so the check after collection does not itself
 * create a live root word while the collection is running. */
static uintptr_t g_hidden_address;
static void *g_bad_ptr;

static void child_release(void) {
  (void)cplus_release(g_bad_ptr);
  _Exit(0); /* the deterministic double-free trap must have exited first */
}

#if defined(__unix__) || defined(__APPLE__)
static int expect_fatal_exit(void (*fn)(void), int expected) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0)
    return 0;
  if (pid == 0) {
    (void)freopen("/dev/null", "w", stderr);
    fn();
    _Exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid)
    return 0;
  return WIFEXITED(status) && WEXITSTATUS(status) == expected;
}
#else
static int expect_fatal_exit(void (*fn)(void), int expected) {
  (void)fn;
  (void)expected;
  return 1; /* platforms without fork(): trap children are not exercised */
}
#endif

int main(int argc, char **argv) {
  int want_gc = 0;

  if (argc > 1 && strcmp(argv[1], "gc-on") == 0)
    want_gc = 1;
  CHECK(cplus_reclaim_enabled() == want_gc);

  /* --- ABI invariants: no fat pointers, no layout perturbation ---------- */
  {
    struct cplus_abi_probe {
      char c;
      void *p;
      int i;
    } probe;
    (void)probe;
    CHECK(sizeof(void *) == sizeof(uintptr_t));
    CHECK(offsetof(struct cplus_abi_probe, p) % _Alignof(void *) == 0u);
    CHECK(offsetof(struct cplus_abi_probe, i) ==
          offsetof(struct cplus_abi_probe, p) + sizeof(void *));
  }

  if (!want_gc) {
    /* Default control: the epoch collector is untouched and a release keeps
     * the historical immediate-free behaviour.  The retired ledger still
     * records the address, so double-free detection is unaffected either way. */
    void *p = cplus_alloc(64u);
    CHECK(p != NULL);
    cplus_release(p);
    CHECK(cplus_reclaim_retired_count() == 0u);
    CHECK(cplus_reclaim_contains_retired(p) == 0);
    CHECK(cplus_address_retired(p) == 1);
    puts("test_reclaim: gc-off ok");
    return 0;
  }

  /* --- gc-on: retain a rooted block across epoch boundaries ----------- */
  {
    volatile void *keep = NULL; /* conservative stack root */
    void *p = cplus_alloc(64u);
    uint64_t epoch0;
    uint64_t reclaimed_before;
    size_t retired_before;

    CHECK(p != NULL);
    memset(p, 0x5au, 64u);
    keep = p;
    CHECK(keep == p); /* read it back: it is deliberately a live root word */
    g_global_root = p;
    cplus_reclaim_register_root(&g_global_root);

    epoch0 = cplus_reclaim_epoch();
    cplus_release(p);
    CHECK(cplus_reclaim_contains_retired(p) == 1);
    CHECK(cplus_reclaim_retired_count() == 1u);

    /* Epoch 1: grace period retains it even if no root existed. */
    cplus_reclaim_collect();
    CHECK(cplus_reclaim_epoch() == epoch0 + 1u);
    CHECK(cplus_reclaim_contains_retired(p) == 1);
    /* Epoch 2: the grace period is over; the roots must retain it. */
    cplus_reclaim_collect();
    CHECK(cplus_reclaim_epoch() == epoch0 + 2u);
    CHECK(cplus_reclaim_contains_retired(p) == 1);
    CHECK(cplus_reclaim_reclaimed_count() == 0u);

    /* The payload is still ours: malloc never got the block back. */
    for (size_t i = 0u; i < 64u; ++i)
      CHECK(((unsigned char *)p)[i] == 0x5au);

    /* Bounded brute force with BOTH allocators: no same-size allocation may
     * be handed the retired address while the root is still live. */
    for (int i = 0; i < 32; ++i) {
      void *q = cplus_alloc(64u);
      CHECK(q != NULL);
      CHECK(q != p);
      memset(q, 0xaau, 64u);
      cplus_release(q);
    }
    for (int i = 0; i < 32; ++i) {
      void *m = malloc(64u);
      CHECK(m != NULL);
      CHECK(m != p);
      free(m);
    }
    CHECK(((unsigned char *)p)[0] == 0x5au);
    CHECK(((unsigned char *)p)[63] == 0x5au);
    CHECK(cplus_reclaim_contains_retired(p) == 1);

    /* Double-free detection is preserved for a retained (not yet reclaimed)
     * block: a second release is a deterministic 104 in a fork child. */
    g_bad_ptr = p;
#if defined(__unix__) || defined(__APPLE__)
    CHECK(expect_fatal_exit(child_release, CPLUS_EXIT_DOUBLE_FREE) == 1);
#endif

    /* --- unreferenced blocks do make progress --------------------------
     * Release every root, hide the address behind an XOR mask so no scanned
     * word points at it, then advance past the grace epoch. */
    reclaimed_before = cplus_reclaim_reclaimed_count();
    retired_before = cplus_reclaim_retired_count();
    g_hidden_address = (uintptr_t)p ^ MASK;
    keep = NULL;
    g_global_root = NULL;
    g_bad_ptr = NULL; /* otherwise the double-free fixture is itself a root */
    cplus_reclaim_unregister_root(&g_global_root);
    p = NULL;

    cplus_reclaim_collect();
    cplus_reclaim_collect();
    CHECK(cplus_reclaim_reclaimed_count() > reclaimed_before);
    CHECK(cplus_reclaim_retired_count() < retired_before);
    CHECK(cplus_reclaim_contains_retired(
              (void *)(g_hidden_address ^ MASK)) == 0);
  }

  puts("test_reclaim: gc-on ok");
  return 0;
}
