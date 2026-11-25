/* Resource-exhaustion resilience: huge collections bounded by a pinned
 * STDLIB_ALLOC_LIMIT.
 *
 * STDLIB_ALLOC_LIMIT is defined to 4096 bytes in THIS translation unit, before
 * any stdlib header, so "huge" means a few thousand bytes rather than a TiB.
 * Requests at or beyond the limit are refused by integer guards before any
 * allocation is attempted; stdlib_alloc_seq() is used to prove zero allocation
 * attempts, not merely a NULL/(-1) return.  No host memory is stressed: the
 * largest successful buffer in this test is 4096 bytes.
 *
 * Self-checking: "<tag> <1|0>" per check, final
 * "RESOURCE_EXHAUSTION <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define STDLIB_ALLOC_LIMIT ((size_t)4096)

#include "cplus/cplus.h"
#include "stdlib/collections.hp"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "tests/security/resource_exhaustion/support/re_harness.hp"

#define RE_HC_ELEMENTS ((STDLIB_ALLOC_LIMIT / sizeof(int)) + 1u)

int main(void) {
    stdlib_telemetry_stats re_hc_tm;
    long re_hc_seq0;
    long re_hc_seq1;
    int re_hc_out = 0;
    int re_hc_i;

    /* Arm telemetry BEFORE constructing any owning container: a constructor's
     * allocation must be counted so alloc/free balance is meaningful. */
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    vector re_hc_vec = vector();
    deque re_hc_deq = deque();
    intmap re_hc_map = intmap();
    string re_hc_str = string("x");

    /* ---- vector: over-limit reserve is refused with zero allocations ---- */
    re_hc_seq0 = stdlib_alloc_seq();
    re_check("re_hc_vec_reserve_refused",
             re_hc_vec.reserve(RE_HC_ELEMENTS) == -1);
    re_hc_seq1 = stdlib_alloc_seq();
    re_check("re_hc_vec_reserve_no_alloc", re_hc_seq1 == re_hc_seq0);
    re_check("re_hc_vec_state_unchanged",
             re_hc_vec.size() == 0 && re_hc_vec.capacity() == 0);

    /* A request exactly at the limit is allowed (4096 B) and stays writable. */
    re_check("re_hc_vec_max_allowed",
             re_hc_vec.reserve(STDLIB_ALLOC_LIMIT / sizeof(int)) == 0);
    re_check("re_hc_vec_max_capacity",
             re_hc_vec.capacity() == STDLIB_ALLOC_LIMIT / sizeof(int));
    re_hc_vec.push_back(7);
    re_check("re_hc_vec_at_checked", re_hc_vec.at_checked(0, &re_hc_out) == 0 &&
                                     re_hc_out == 7);
    /* Fill the limit-sized buffer (bounded: 1023 more ints, 4096 B total),
     * then verify growth past the limit is refused without an allocation. */
    for (re_hc_i = 1; re_hc_i < (int)(STDLIB_ALLOC_LIMIT / sizeof(int)); re_hc_i++)
        re_hc_vec.push_back(re_hc_i);
    re_hc_seq0 = stdlib_alloc_seq();
    /* A full 4096-byte buffer is the documented maximum; a further push
     * that stays within capacity is a normal write, not a growth request.
     * Exercise refusal via an explicit over-limit reserve instead. */
    re_check("re_hc_vec_growth_refused",
             re_hc_vec.reserve(STDLIB_ALLOC_LIMIT / sizeof(int) + 1u) == -1);
    re_hc_seq1 = stdlib_alloc_seq();
    re_check("re_hc_vec_growth_no_alloc", re_hc_seq1 == re_hc_seq0);
    re_hc_vec.drop();

    /* ---- deque: ring copy enforces the same byte ceiling ---------------- */
    re_hc_seq0 = stdlib_alloc_seq();
    re_check("re_hc_deq_reserve_refused",
             re_hc_deq.reserve(RE_HC_ELEMENTS) == -1);
    re_hc_seq1 = stdlib_alloc_seq();
    re_check("re_hc_deq_reserve_no_alloc", re_hc_seq1 == re_hc_seq0);
    re_check("re_hc_deq_small_ok", re_hc_deq.reserve(8) == 0);
    re_hc_deq.push_back(11);
    re_check("re_hc_deq_at_checked",
             re_hc_deq.at_checked(0, &re_hc_out) == 0 && re_hc_out == 11);
    re_hc_deq.drop();

    /* ---- intmap: hash table rebuild refuses a limit-sized capacity ------ */
    re_hc_seq0 = stdlib_alloc_seq();
    re_check("re_hc_map_grow_refused",
             re_hc_map.grow(STDLIB_ALLOC_LIMIT) == -1);
    re_hc_seq1 = stdlib_alloc_seq();
    re_check("re_hc_map_grow_no_alloc", re_hc_seq1 == re_hc_seq0);
    re_check("re_hc_map_small_ok", re_hc_map.grow(8) == 0);
    re_hc_map.put(3, 33);
    re_hc_out = 0;
    re_check("re_hc_map_get",
             re_hc_map.get(3, &re_hc_out) == 0 && re_hc_out == 33);
    re_hc_map.drop();

    /* ---- string: reserve beyond the limit is refused atomically --------- */
    re_hc_seq0 = stdlib_alloc_seq();
    re_check("re_hc_str_reserve_refused",
             re_hc_str.reserve(STDLIB_ALLOC_LIMIT + 1u) == -1);
    re_hc_seq1 = stdlib_alloc_seq();
    re_check("re_hc_str_reserve_no_alloc", re_hc_seq1 == re_hc_seq0);
    re_check("re_hc_str_unchanged", re_hc_str.equals("x") == 1);
    re_hc_str.drop();

    /* Everything that was allocated was released; refused requests never
     * reached the host allocator and left no metadata behind. */
    stdlib_telemetry_snapshot(&re_hc_tm);
    re_check("re_hc_telemetry_balanced",
             re_hc_tm.alloc_count == re_hc_tm.free_count);
    re_check("re_hc_no_limit_failure", re_hc_tm.alloc_failures == 0ULL);

    stdlib_telemetry_set_enabled(0);
    stdlib_alloc_fail_reset();
    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
