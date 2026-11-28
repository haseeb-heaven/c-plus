/* Linux: pthread_getattr_np() in the included runtime needs GNU feature
 * macros declared before the first libc header in this TU as well. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

/* Deterministic C regression tests for issues #181 and #182.
 *
 * Issue #181 additionally covers the opt-in `-fcplus-disjoint-bounds` runtime
 * contract: pointer-slot seed/copy propagation, deterministic OOB/UAF traps,
 * stack-frame key registration/invalidation and O(1) detection (the linear
 * introspection scan counter must stay at zero on the check path).
 *
 * The test compiles the runtime implementation directly (the same pattern as
 * tests/runtime/issues_164_166) so the additive introspection API is checked
 * against the real live-table side table.  No project build, game assets or
 * external services are involved.
 *
 * Covered:
 *   #181 disjoint (base, bound) lookup, including interior pointers;
 *   #181 CETS generation-checked interior validation with a deterministic
 *        same-address generation change (ABA);
 *   #182 low-fat size-class mapping, class metadata on live slots, and
 *        interior base/size resolution;
 *   #182 CPLUS_LOWFAT_ALLOC opt-in, segregated size-class VM regions, O(1)
 *        lock-free bounds lookup (including a pointer round-tripped through a
 *        separately compiled plain-C translation unit), and out-of-band type
 *        tags with deterministic 105 traps;
 *   both  graceful handling of untracked pointers (plain-C ABI boundary) with
 *         no fatal hook calls in the parent process.
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

/* Pull in the implementation so internal side-table state is the real thing. */
#include "../../../runtime/cplus_runtime.c"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Implemented in uninstrumented_helper.c, compiled as plain C with no runtime
 * header at all: the pointer crosses an ordinary C ABI boundary and comes back
 * bit-identical. */
struct cplus_test_opaque {
  void *ptr;
  uintptr_t copy;
  unsigned marker;
};
void *cplus_test_roundtrip(void *p, void *opaque);

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int fatal_calls;
static void count_fatal(cplus_exit_code code, const char *kind) {
  (void)code;
  (void)kind;
  ++fatal_calls;
}

/* Trap checks run in a child process because cplus_fatal() terminates. */
static void *g_slot;
static void *g_slot2;
static const void *g_access;
static size_t g_bytes;
static uint64_t g_frame_key;
static void trap_access(void) {
  (void)cplus_shadow_slot_check(&g_slot, g_access, g_bytes, __FILE__, __LINE__);
}

#if defined(__unix__) || defined(__APPLE__)
static int exits_with(int wanted) {
  pid_t pid = fork();
  if (pid == 0) {
    (void)freopen("/dev/null", "w", stderr);
    trap_access();
    _Exit(0); /* the check must not return for a trapped access */
  }
  int status = 0;
  (void)waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == wanted;
}
#endif

#if defined(__unix__) || defined(__APPLE__)
static int expect_fatal_exit(void (*fn)(void), int expected) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0)
    return 0;
  if (pid == 0) {
    fn();
    _Exit(0); /* the trap must have exited first */
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

static void *g_bad_ptr;
static cplus_type_tag g_bad_tag;
static void child_wrong_tag(void) {
  cplus_type_tag_check(g_bad_ptr, g_bad_tag, __FILE__, __LINE__);
}
static void child_null_tag(void) {
  cplus_type_tag_check(NULL, g_bad_tag, __FILE__, __LINE__);
}
static void *g_member_ptr;
static cplus_type_tag g_parent_tag, g_member_tag;
static size_t g_offset, g_bytes;
static void child_wrong_member(void) {
  cplus_type_member_check(g_member_ptr, g_parent_tag, g_member_tag, g_offset,
                          g_bytes, __FILE__, __LINE__);
}

#if UINTPTR_MAX > 0xffffffffULL
#define TEST_REGION_SHIFT 24u /* matches CPLUS_LOWFAT_REGION_SHIFT on 64-bit */
#else
#define TEST_REGION_SHIFT 20u
#endif

int main(int argc, char **argv) {
  cplus_shadow_info info;
  void *base = NULL;
  void *bound = NULL;
  size_t size = 0;

  cplus_fatal_hook = count_fatal;

  /* run.sh runs the same binary on both sides of the env opt-in. */
  if (argc > 1 && strcmp(argv[1], "env-on") == 0) {
    CHECK(cplus_lowfat_enabled() == 1);
  } else if (argc > 1 && strcmp(argv[1], "env-off") == 0) {
    CHECK(cplus_lowfat_enabled() == 0);
  }

  /* --- ABI invariants: no fat pointers, no layout perturbation ---------- */
  {
    struct cplus_abi_probe {
      char c;
      void *p;
      int i;
    } probe;
    (void)probe;
    CHECK(sizeof(void *) == sizeof(uintptr_t));
    CHECK(_Alignof(void *) != 0u);
    CHECK(offsetof(struct cplus_abi_probe, p) % _Alignof(void *) == 0u);
    CHECK(offsetof(struct cplus_abi_probe, i) ==
          offsetof(struct cplus_abi_probe, p) + sizeof(void *));
  }

  /* --- #182: pure size-class mapping (no lock, no allocation) ------------ */
  CHECK(cplus_lowfat_size_class(0u) == 0);
  CHECK(cplus_lowfat_size_class(1u) == 1);
  CHECK(cplus_lowfat_size_class(16u) == 1);
  CHECK(cplus_lowfat_size_class(17u) == 2);
  CHECK(cplus_lowfat_size_class(256u) == 16);
  CHECK(cplus_lowfat_size_class(257u) == 17);
  CHECK(cplus_lowfat_size_class(512u) == 17);
  CHECK(cplus_lowfat_size_class(513u) == 18);

  /* --- graceful uninstrumented-C boundary: untracked pointers ----------- */
  CHECK(cplus_shadow_lookup(NULL, &info) == 0);
  CHECK(info.base == NULL && info.bound == NULL && info.generation == 0u);
  CHECK(info.lock == NULL && info.flags == 0u);
  {
    int stack_value = 0;
    CHECK(cplus_shadow_lookup(&stack_value, &info) == 0);
    CHECK(info.base == NULL);
    CHECK(cplus_shadow_base_bound(&stack_value, &base, &bound) == 0);
    CHECK(cplus_lowfat_bounds(&stack_value, &base, &size) == 0);
    CHECK(cplus_lowfat_class_of(&stack_value) == -1);
    CHECK(cplus_interior_valid(&stack_value, 1u) == 0);
  }
  {
    void *raw = malloc(64u);
    CHECK(raw != NULL);
    /* A malloc block produced outside the tracked paths is ordinary C. */
    CHECK(cplus_shadow_lookup(raw, &info) == 0);
    CHECK(cplus_lowfat_bounds(raw, &base, &size) == 0);
    CHECK(cplus_interior_valid(raw, 1u) == 0);
    free(raw);
  }
  CHECK(fatal_calls == 0);

  /* --- #181/#182: disjoint base/bound for a live managed allocation ----- */
  {
    void *p = cplus_alloc(64u);
    uint64_t g;

    CHECK(p != NULL);
    g = cplus_allocation_generation(p);
    CHECK(g != 0u);

    memset(&info, 0xff, sizeof(info));
    CHECK(cplus_shadow_lookup(p, &info) == 1);
    CHECK(info.base == p);
    CHECK(info.bound == (void *)((char *)p + 64u));
    CHECK(info.generation == g);
    CHECK(info.lock != NULL);
    CHECK((info.flags & CPLUS_SHADOW_LIVE) != 0u);
    CHECK((info.flags & CPLUS_SHADOW_INTERIOR) == 0u);

    /* Interior byte: same disjoint base/bound, INTERIOR bit set. */
    memset(&info, 0xff, sizeof(info));
    CHECK(cplus_shadow_lookup((char *)p + 16u, &info) == 1);
    CHECK(info.base == p);
    CHECK(info.bound == (void *)((char *)p + 64u));
    CHECK(info.generation == g);
    CHECK((info.flags & CPLUS_SHADOW_INTERIOR) != 0u);

    base = NULL;
    bound = NULL;
    CHECK(cplus_shadow_base_bound((char *)p + 63u, &base, &bound) == 1);
    CHECK(base == p && bound == (void *)((char *)p + 64u));

    /* Strict [base, bound): before base and one-past-the-end are uncovered. */
    CHECK(cplus_shadow_lookup((void *)((uintptr_t)p - 1u), &info) == 0);
    CHECK(cplus_shadow_lookup((char *)p + 64u, &info) == 0);

    /* #182: low-fat resolution and the class recorded on the live slot. */
    base = NULL;
    size = 0;
    CHECK(cplus_lowfat_bounds((char *)p + 8u, &base, &size) == 1);
    CHECK(base == p && size == 64u);
    CHECK(cplus_lowfat_class_of(p) == 4);
    CHECK(cplus_lowfat_class_of((char *)p + 8u) ==
          cplus_lowfat_size_class(64u));

    /* #181 CETS: generation-checked interior validation. */
    CHECK(cplus_interior_valid((char *)p + 8u, g) == 1);
    CHECK(cplus_interior_valid((char *)p + 8u, g + 1u) == 0);
    CHECK(cplus_interior_valid((char *)p + 8u, 0u) == 0);

    cplus_release(p);
    CHECK(cplus_shadow_lookup(p, &info) == 0);
    CHECK(cplus_interior_valid((char *)p + 8u, g) == 0);
  }

  /* --- #181 CETS: same-address generation change (ABA) ------------------ */
  {
    void *raw = malloc(64u);
    uint64_t g1, g2;
    void *r, *r2;

    CHECK(raw != NULL);
    r = cplus_register_allocation(raw, 64u, __FILE__, __LINE__);
    CHECK(r == raw);
    g1 = cplus_allocation_generation(r);
    CHECK(g1 != 0u);
    CHECK(cplus_interior_valid((char *)r + 8u, g1) == 1);

    /* In-place registered realloc keeps the address but issues a new key. */
    r2 = cplus_realloc_registered(r, r, 64u, __FILE__, __LINE__);
    CHECK(r2 == r);
    g2 = cplus_allocation_generation(r);
    CHECK(g2 != g1);
    CHECK(cplus_interior_valid((char *)r + 8u, g1) == 0); /* stale key */
    CHECK(cplus_interior_valid((char *)r + 8u, g2) == 1); /* current key */

    cplus_free_registered(r, __FILE__, __LINE__);
    CHECK(cplus_shadow_lookup(r, &info) == 0);
    CHECK(cplus_interior_valid((char *)r + 8u, g2) == 0);
  }

  /* --- #182: segregated regions + O(1) lock-free bounds ----------------- */
  cplus_lowfat_set_enabled(1); /* deterministic even in the env-off run */
  CHECK(cplus_lowfat_enabled() == 1);
  {
    size_t live_before = cplus_lowfat_live_count();
    uint64_t lookups_before = cplus_lowfat_lockfree_lookups();
    void *a = cplus_alloc(24u); /* class 2, 32-byte slot */
    void *b = cplus_alloc(600u); /* class 18, 1024-byte slot */
    struct cplus_test_opaque box;
    void *interior = (char *)a + 11u;
    void *roundtrip;
    cplus_type_tag tag_a, tag_b, member;
    uint64_t gen_a;

    CHECK(a != NULL && b != NULL);
    CHECK(cplus_lowfat_type_meta_enabled() == 1);
    CHECK(cplus_lowfat_live_count() == live_before + 2u);

    /* Segregated size-class VM regions: different classes live at different
     * region indices, not in one shared malloc heap. */
    CHECK(cplus_lowfat_contains(a) == 1);
    CHECK(cplus_lowfat_contains((char *)b + 599u) == 1);
    CHECK(cplus_lowfat_class_of(a) == cplus_lowfat_size_class(24u));
    CHECK(cplus_lowfat_class_of(b) == cplus_lowfat_size_class(600u));
    CHECK(((uintptr_t)a >> TEST_REGION_SHIFT) !=
          ((uintptr_t)b >> TEST_REGION_SHIFT));

    /* Interior pointer round-tripped through a separately compiled plain C
     * translation unit (no runtime header): still a bare address, still O(1)
     * resolvable.  Exactly one lock-free hit per cplus_lowfat_bounds call. */
    roundtrip = cplus_test_roundtrip(interior, &box);
    CHECK(roundtrip == interior && box.marker == 0xc0ffeeu);
    base = NULL;
    size = 0;
    lookups_before = cplus_lowfat_lockfree_lookups();
    CHECK(cplus_lowfat_bounds(roundtrip, &base, &size) == 1);
    CHECK(base == a && size == 24u);
    CHECK(cplus_lowfat_lockfree_lookups() == lookups_before + 1u);
    gen_a = cplus_allocation_generation(a);
    CHECK(cplus_interior_valid(roundtrip, gen_a) == 1);
    CHECK(cplus_interior_valid(roundtrip, gen_a + 1u) == 0);

    /* Out-of-band effective type tags: attach/get/check never change layout. */
    tag_a = cplus_type_tag_register("struct alpha", 24u);
    tag_b = cplus_type_tag_register("struct beta", 24u);
    member = cplus_type_tag_register("alpha.field", 4u);
    CHECK(tag_a != 0u && tag_b != 0u && tag_a != tag_b && member != 0u);
    CHECK(cplus_type_tag_register("struct alpha", 24u) == tag_a);
    CHECK(cplus_type_tag_hash("struct alpha", 24u) != 0u);
    CHECK(cplus_type_tag_set(a, tag_a) == 1);
    CHECK(cplus_type_tag_get((char *)a + 3u) == tag_a);
    cplus_type_check((char *)a + 3u, tag_a); /* must not trap */
    CHECK(fatal_calls == 0);

    g_bad_ptr = a;
    g_bad_tag = tag_b;
    CHECK(expect_fatal_exit(child_wrong_tag, CPLUS_EXIT_TYPE_CONFUSION) == 1);
    CHECK(expect_fatal_exit(child_null_tag, CPLUS_EXIT_TYPE_CONFUSION) == 1);

    /* Downcast/member checks: registered member passes, wrong tag and
     * out-of-range access trap with the same deterministic code. */
    CHECK(cplus_type_member_register(tag_a, "field", 8u, 4u, member) == 1);
    cplus_type_check_member(a, tag_a, member, 8u, 4u);
    CHECK(fatal_calls == 0);
    g_member_ptr = a;
    g_parent_tag = tag_a;
    g_member_tag = tag_b; /* wrong member tag at a registered offset */
    g_offset = 8u;
    g_bytes = 4u;
    CHECK(expect_fatal_exit(child_wrong_member, CPLUS_EXIT_TYPE_CONFUSION) == 1);
    g_member_tag = member;
    g_offset = 20u; /* 20 + 8 > 24-byte object */
    g_bytes = 8u;
    CHECK(expect_fatal_exit(child_wrong_member, CPLUS_EXIT_TYPE_CONFUSION) == 1);

    /* O(1) means independent of the live set: with 64 additional live slots,
     * one interior lookup is still exactly one lock-free hit. */
    {
      void *many[64];
      for (size_t i = 0u; i < 64u; ++i) {
        many[i] = cplus_alloc(32u);
        CHECK(many[i] != NULL);
      }
      CHECK(cplus_lowfat_live_count() == live_before + 66u);
      lookups_before = cplus_lowfat_lockfree_lookups();
      base = NULL;
      size = 0;
      CHECK(cplus_lowfat_bounds((char *)many[63] + 8u, &base, &size) == 1);
      CHECK(base == many[63] && size == 32u);
      CHECK(cplus_lowfat_lockfree_lookups() == lookups_before + 1u);
      for (size_t i = 0u; i < 64u; ++i)
        cplus_release(many[i]);
      CHECK(cplus_lowfat_live_count() == live_before + 2u);
    }

    cplus_release(a);
    cplus_release(b);
    CHECK(cplus_lowfat_live_count() == live_before);
    CHECK(cplus_lowfat_bounds((char *)a + 7u, &base, &size) == 0);
    CHECK(cplus_type_tag_get(a) == 0u);
  }

  /* --- graceful fallback when the opt-in is turned back off ------------- */
  cplus_lowfat_set_enabled(0);
  CHECK(cplus_lowfat_enabled() == 0);
  {
    void *p = cplus_alloc(64u);
    uint64_t lookups_before = cplus_lowfat_lockfree_lookups();
    CHECK(p != NULL);
    CHECK(cplus_lowfat_contains(p) == 0);
    base = NULL;
    size = 0;
    CHECK(cplus_lowfat_bounds((char *)p + 8u, &base, &size) == 1);
    CHECK(base == p && size == 64u);
    /* Non-low-fat pointers keep the original mutex/side-table path. */
    CHECK(cplus_lowfat_lockfree_lookups() == lookups_before);
    cplus_release(p);
  }

  /* --- #181: opt-in disjoint-bounds slot metadata and traps ------------ */
  {
    void *p = cplus_alloc(64u);
    CHECK(p != NULL);
    g_slot = p;
    cplus_shadow_slot_seed(&g_slot, p);

    CHECK(cplus_shadow_slot_check(&g_slot, p, 64u, __FILE__, __LINE__) == 1);
    CHECK(cplus_shadow_slot_check(&g_slot, (char *)p + 63u, 1u, __FILE__,
                                  __LINE__) == 1);
    /* One-past-the-end is legal for a zero-byte access. */
    CHECK(cplus_shadow_slot_check(&g_slot, (char *)p + 64u, 0u, __FILE__,
                                  __LINE__) == 1);

    /* Pointer-slot propagation: an interior alias inherits base/bound/key. */
    g_slot2 = (char *)p + 8u;
    cplus_shadow_slot_copy(&g_slot2, &g_slot);
    CHECK(cplus_shadow_slot_check(&g_slot2, g_slot2, 8u, __FILE__, __LINE__) ==
          1);
    cplus_shadow_slot_copy(&g_slot, &g_slot2);
    CHECK(cplus_shadow_slot_check(&g_slot, g_slot2, 8u, __FILE__, __LINE__) ==
          1);

#if defined(__unix__) || defined(__APPLE__)
    g_access = (char *)p + 64u; /* strictly outside [base, bound) */
    g_bytes = 1u;
    CHECK(exits_with(CPLUS_EXIT_BOUNDS));
#endif

    /* O(1) detection: the linear introspection scan is never on this path.
     * Low-fat interior lookups take a different O(1) path, so force the
     * classic side-table path for the scan-counter contrast. */
    {
      int lowfat_was = cplus_lowfat_enabled();
      cplus_lowfat_set_enabled(0);
      cplus_shadow_reset_probes();
      for (int i = 0; i < 1000; ++i)
        CHECK(cplus_shadow_slot_check(&g_slot, (char *)p + (i % 64), 1u,
                                      __FILE__, __LINE__) == 1);
      CHECK(cplus_shadow_scan_probes() == 0u);
      CHECK(cplus_shadow_lookup((char *)p + 8u, &info) == 1);
      CHECK(cplus_shadow_scan_probes() > 0u); /* interior lookup is the scan */
      cplus_lowfat_set_enabled(lowfat_was);
    }

#if defined(__unix__) || defined(__APPLE__)
    /* Temporal invalidation is O(1): release zeroes the allocation's lock. */
    cplus_release(p);
    g_access = p;
    g_bytes = 1u;
    CHECK(exits_with(CPLUS_EXIT_USE_AFTER_FREE));
#endif
  }

  /* --- #181: stack-frame key registration / invalidation --------------- */
  {
    int arr[4] = {0, 0, 0, 0};
    g_frame_key = cplus_shadow_frame_enter();
    CHECK(g_frame_key != 0u);
    CHECK(cplus_shadow_frame_valid(g_frame_key) == 1);
    CHECK(cplus_shadow_frame_register(&g_slot, &g_frame_key, arr,
                                      sizeof(arr)) == 1);
    g_slot = arr;
    CHECK(cplus_shadow_slot_check(&g_slot, &arr[3], sizeof(int), __FILE__,
                                  __LINE__) == 1);
#if defined(__unix__) || defined(__APPLE__)
    g_access = &arr[4]; /* one past the registered stack object */
    g_bytes = sizeof(int);
    CHECK(exits_with(CPLUS_EXIT_BOUNDS));
#endif

    cplus_shadow_frame_leave(&g_frame_key);
    CHECK(g_frame_key == 0u);
    CHECK(cplus_shadow_frame_valid(g_frame_key) == 0);
#if defined(__unix__) || defined(__APPLE__)
    g_access = &arr[0];
    g_bytes = sizeof(int);
    CHECK(exits_with(CPLUS_EXIT_USE_AFTER_FREE));
#endif

    /* Clearing a slot returns it to plain-C (untracked) behaviour. */
    cplus_shadow_slot_clear(&g_slot);
    CHECK(cplus_shadow_slot_check(&g_slot, &arr[0], sizeof(int), __FILE__,
                                  __LINE__) == 1);
  }

#if defined(__unix__) || defined(__APPLE__)
  /* NULL is trapped only for a tracked slot. */
  {
    void *p = cplus_alloc(8u);
    CHECK(p != NULL);
    g_slot = p;
    cplus_shadow_slot_seed(&g_slot, p);
    g_slot = NULL;
    g_access = NULL;
    g_bytes = 1u;
    CHECK(exits_with(CPLUS_EXIT_NULL_POINTER));
    cplus_shadow_slot_clear(&g_slot);
    cplus_release(p);
  }
#endif

  CHECK(fatal_calls == 0);
  puts("feature_shadow_metadata: ok");
  return 0;
}
