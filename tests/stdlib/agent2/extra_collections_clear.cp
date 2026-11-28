/* Agent 2 — extra collections clear()/limit/overflow regression test.
 *
 * Closes the UNTESTED paths in stdlib/collections.hp identified by the final
 * audit: clear() on populated containers, repeated clear() then reuse,
 * strmap's oversized-key guard, the doubling-overflow guards in deque/stack/
 * queue, stdlib_map_rebuild_capacity's overflow return, and count/used_slots
 * consistency after tombstones + clear (inferred only from observable
 * behaviour: sizes, lookups, and allocator telemetry).
 *
 * Build note: every build command for this file passes
 *   -DSTDLIB_ALLOC_LIMIT=256
 * so that an "oversized" key is 256 bytes and a capacity-doubling guard is
 * reached after 64 ints.  With the default 1<<40 limit both guards would need
 * memory exhaustion to reach, which this test deliberately avoids.  A matching
 * source-level #define before the includes is also required because the C+
 * front-end's strict bounds checker does not see -D flags (only the C backend
 * does); the two values are identical.  No header is edited.  The runtime
 * guard at the top of the oversized-key section makes the file safe even if
 * both overrides are absent.
 *
 * Every check prints "<tag> <0|1>" via the C+ builtin print().  The last line
 * is "EXTRA_COLLECTIONS_CLEAR <checks> <failures>" and the exit code is 0 only
 * when failures == 0.  All owned locals have unique ecc_ names because the
 * C+ RAII lowering tracks owners by name for a whole function.  Telemetry
 * epochs start before the owned-locals block (constructors are hoisted to the
 * top of their block) and the final snapshot is taken before that block ends.
 */
#define STDLIB_ALLOC_LIMIT 256
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

static int ecc_checks = 0;
static int ecc_failures = 0;

static void ecc_check(const char *ecc_tag, int ecc_condition) {
    ecc_checks++;
    if (!ecc_condition)
        ecc_failures++;
    print(ecc_tag, ecc_condition ? 1 : 0);
}

/* Scratch for keys at/over the (overridden) allocation limit.  512 bytes is
 * plenty for STDLIB_ALLOC_LIMIT=256; the runtime guard below rejects larger
 * limits instead of overflowing this array. */
static char ecc_bigkey[512];

int main(void) {
    stdlib_telemetry_stats ecc_pre;
    stdlib_telemetry_stats ecc_post;
    int ecc_final_rc = 0;
    size_t ecc_limit = (size_t)STDLIB_ALLOC_LIMIT;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);

    {
        int ecc_i = 0;
        int ecc_v = 0;
        int ecc_rc = 0;
        int ecc_good = 1;
        size_t ecc_cap = 0;
        size_t ecc_sz = 0;

        /* ---------------- intset: clear, reuse, triple clear, rebuild ---- */
        intset ecc_set = intset();
        for (ecc_i = 0; ecc_i < 6; ecc_i++)
            if (ecc_set.insert(100 + ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_set_populated", ecc_good == 1 && ecc_set.size() == 6 &&
                  ecc_set.contains(100) == 1 && ecc_set.contains(105) == 1);
        ecc_set.clear();
        ecc_check("ecc_set_clear_size", ecc_set.size() == 0 &&
                  ecc_set.contains(100) == 0 && ecc_set.contains(105) == 0);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 6; ecc_i++)
            if (ecc_set.insert(200 + ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_set_reuse", ecc_good == 1 && ecc_set.size() == 6 &&
                  ecc_set.contains(200) == 1 && ecc_set.contains(205) == 1 &&
                  ecc_set.contains(100) == 0);
        ecc_set.remove(200);
        ecc_set.remove(201); /* tombstones, then clear() must reset slots */
        ecc_set.clear();
        ecc_set.clear();
        ecc_set.clear();
        ecc_check("ecc_set_triple_clear", ecc_set.size() == 0 &&
                  ecc_set.contains(205) == 0 && ecc_set.contains(200) == 0);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 24; ecc_i++)
            if (ecc_set.insert(300 + ecc_i) != 0) ecc_good = 0;
        for (ecc_i = 0; ecc_i < 24; ecc_i++)
            if (ecc_set.contains(300 + ecc_i) != 1) ecc_good = 0;
        ecc_check("ecc_set_rebuild_after_clear",
                  ecc_good == 1 && ecc_set.size() == 24 &&
                  ecc_set.contains(323) == 1);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_set.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_set_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.ownership_violations == 0);

        /* ---------------- deque: clear, reuse, triple clear -------------- */
        deque ecc_deq = deque();
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 6; ecc_i++)
            if (ecc_deq.push_back(ecc_i) != 0) ecc_good = 0;
        if (ecc_deq.push_front(99) != 0) ecc_good = 0;
        ecc_v = -1;
        ecc_check("ecc_deq_populated", ecc_good == 1 && ecc_deq.size() == 7 &&
                  ecc_deq.at_checked(0, &ecc_v) == 0 && ecc_v == 99 &&
                  ecc_deq.at_checked(1, &ecc_v) == 0 && ecc_v == 0 &&
                  ecc_deq.at_checked(6, &ecc_v) == 0 && ecc_v == 5);
        ecc_deq.clear();
        ecc_check("ecc_deq_clear_size", ecc_deq.size() == 0 &&
                  ecc_deq.pop_front(&ecc_v) == -1 &&
                  ecc_deq.pop_back(&ecc_v) == -1);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 8; ecc_i++)
            if (ecc_deq.push_back(700 + ecc_i) != 0) ecc_good = 0;
        if (ecc_deq.push_front(699) != 0) ecc_good = 0;
        ecc_check("ecc_deq_reuse",
                  ecc_good == 1 && ecc_deq.size() == 9 &&
                  ecc_deq.at_checked(0, &ecc_v) == 0 && ecc_v == 699 &&
                  ecc_deq.at_checked(1, &ecc_v) == 0 && ecc_v == 700 &&
                  ecc_deq.at_checked(8, &ecc_v) == 0 && ecc_v == 707);
        ecc_deq.clear();
        ecc_deq.clear();
        ecc_deq.clear();
        ecc_check("ecc_deq_triple_clear", ecc_deq.size() == 0 &&
                  ecc_deq.pop_front(&ecc_v) == -1);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 5; ecc_i++)
            if (ecc_deq.push_front(ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_deq_reuse_after_triple",
                  ecc_good == 1 && ecc_deq.size() == 5 &&
                  ecc_deq.at_checked(0, &ecc_v) == 0 && ecc_v == 4 &&
                  ecc_deq.at_checked(4, &ecc_v) == 0 && ecc_v == 0);
        /* reserve(SIZE_MAX) must fail in stdlib_ring_copy's limit guard and
         * leave a populated, non-zero-head ring exactly as it was. */
        ecc_sz = ecc_deq.size();
        ecc_cap = ecc_deq.capacity();
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_rc = ecc_deq.reserve((size_t)-1);
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_deq_reserve_max_rejected",
                  ecc_rc == -1 && ecc_deq.size() == ecc_sz &&
                  ecc_deq.capacity() == ecc_cap &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.free_count == ecc_pre.free_count);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_deq.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_deq_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.ownership_violations == 0);

        /* ---------------- list: clear, reuse, triple clear --------------- */
        list ecc_lst = list();
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 5; ecc_i++)
            if (ecc_lst.push_back(ecc_i) != 0) ecc_good = 0;
        if (ecc_lst.push_front(55) != 0) ecc_good = 0;
        ecc_v = -1;
        ecc_check("ecc_list_populated", ecc_good == 1 && ecc_lst.size() == 6 &&
                  ecc_lst.at_checked(0, &ecc_v) == 0 && ecc_v == 55 &&
                  ecc_lst.at_checked(5, &ecc_v) == 0 && ecc_v == 4);
        ecc_lst.clear();
        ecc_check("ecc_list_clear_size", ecc_lst.size() == 0 &&
                  ecc_lst.pop_front(&ecc_v) == -1 &&
                  ecc_lst.pop_back(&ecc_v) == -1);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 4; ecc_i++)
            if (ecc_lst.push_back(800 + ecc_i) != 0) ecc_good = 0;
        if (ecc_lst.push_front(799) != 0) ecc_good = 0;
        ecc_check("ecc_list_reuse",
                  ecc_good == 1 && ecc_lst.size() == 5 &&
                  ecc_lst.at_checked(0, &ecc_v) == 0 && ecc_v == 799 &&
                  ecc_lst.at_checked(4, &ecc_v) == 0 && ecc_v == 803);
        ecc_lst.clear();
        ecc_lst.clear();
        ecc_lst.clear();
        ecc_check("ecc_list_triple_clear", ecc_lst.size() == 0 &&
                  ecc_lst.pop_back(&ecc_v) == -1);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 3; ecc_i++)
            if (ecc_lst.push_front(ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_list_reuse_after_triple",
                  ecc_good == 1 && ecc_lst.size() == 3 &&
                  ecc_lst.at_checked(0, &ecc_v) == 0 && ecc_v == 2 &&
                  ecc_lst.at_checked(2, &ecc_v) == 0 && ecc_v == 0);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_lst.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_list_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.ownership_violations == 0);

        /* ---------------- stack: clear, reuse, triple clear -------------- */
        stack ecc_stk = stack();
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 6; ecc_i++)
            if (ecc_stk.push(ecc_i) != 0) ecc_good = 0;
        ecc_v = -1;
        ecc_check("ecc_stack_populated", ecc_good == 1 && ecc_stk.size() == 6 &&
                  ecc_stk.top_checked(&ecc_v) == 0 && ecc_v == 5);
        ecc_stk.clear();
        ecc_check("ecc_stack_clear_size", ecc_stk.size() == 0 &&
                  ecc_stk.top_checked(&ecc_v) == -1 &&
                  ecc_stk.pop(&ecc_v) == -1);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 5; ecc_i++)
            if (ecc_stk.push(900 + ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_stack_reuse",
                  ecc_good == 1 && ecc_stk.size() == 5 &&
                  ecc_stk.top_checked(&ecc_v) == 0 && ecc_v == 904 &&
                  ecc_stk.pop(&ecc_v) == 0 && ecc_v == 904 &&
                  ecc_stk.size() == 4);
        ecc_stk.clear();
        ecc_stk.clear();
        ecc_stk.clear();
        ecc_check("ecc_stack_triple_clear", ecc_stk.size() == 0 &&
                  ecc_stk.pop(&ecc_v) == -1);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 4; ecc_i++)
            if (ecc_stk.push(ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_stack_reuse_after_triple",
                  ecc_good == 1 && ecc_stk.size() == 4 &&
                  ecc_stk.top_checked(&ecc_v) == 0 && ecc_v == 3);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_stk.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_stack_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.ownership_violations == 0);

        /* ---------------- queue: clear, reuse, triple clear -------------- */
        queue ecc_que = queue();
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 6; ecc_i++)
            if (ecc_que.push(ecc_i) != 0) ecc_good = 0;
        ecc_v = -1;
        ecc_check("ecc_queue_populated", ecc_good == 1 && ecc_que.size() == 6 &&
                  ecc_que.front_checked(&ecc_v) == 0 && ecc_v == 0 &&
                  ecc_que.pop(&ecc_v) == 0 && ecc_v == 0 &&
                  ecc_que.front_checked(&ecc_v) == 0 && ecc_v == 1);
        ecc_que.clear();
        ecc_check("ecc_queue_clear_size", ecc_que.size() == 0 &&
                  ecc_que.front_checked(&ecc_v) == -1 &&
                  ecc_que.pop(&ecc_v) == -1);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 5; ecc_i++)
            if (ecc_que.push(1000 + ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_queue_reuse",
                  ecc_good == 1 && ecc_que.size() == 5 &&
                  ecc_que.front_checked(&ecc_v) == 0 && ecc_v == 1000 &&
                  ecc_que.pop(&ecc_v) == 0 && ecc_v == 1000 &&
                  ecc_que.front_checked(&ecc_v) == 0 && ecc_v == 1001);
        ecc_que.clear();
        ecc_que.clear();
        ecc_que.clear();
        ecc_check("ecc_queue_triple_clear", ecc_que.size() == 0 &&
                  ecc_que.pop(&ecc_v) == -1);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 4; ecc_i++)
            if (ecc_que.push(ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_queue_reuse_after_triple",
                  ecc_good == 1 && ecc_que.size() == 4 &&
                  ecc_que.front_checked(&ecc_v) == 0 && ecc_v == 0 &&
                  ecc_que.pop(&ecc_v) == 0 && ecc_v == 0);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_que.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_queue_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.ownership_violations == 0);

        /* ---------------- intmap: tombstones + clear, slots consistency -- */
        intmap ecc_map = intmap();
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 8; ecc_i++)
            if (ecc_map.put(ecc_i, ecc_i * 2) != 0) ecc_good = 0;
        ecc_map.remove(0);
        ecc_map.remove(3);
        ecc_map.remove(7);
        ecc_check("ecc_map_tombstones", ecc_good == 1 && ecc_map.size() == 5 &&
                  ecc_map.contains(0) == 0 && ecc_map.contains(1) == 1);
        ecc_map.clear();
        ecc_map.clear();
        ecc_map.clear();
        ecc_check("ecc_map_clear_size", ecc_map.size() == 0 &&
                  ecc_map.contains(1) == 0);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 24; ecc_i++)
            if (ecc_map.put(400 + ecc_i, ecc_i) != 0) ecc_good = 0;
        for (ecc_i = 0; ecc_i < 24; ecc_i++) {
            ecc_v = -1;
            if (ecc_map.get(400 + ecc_i, &ecc_v) != 0 || ecc_v != ecc_i)
                ecc_good = 0;
        }
        ecc_check("ecc_map_rebuild_after_clear",
                  ecc_good == 1 && ecc_map.size() == 24);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_map.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_map_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.ownership_violations == 0);

        /* ---------------- strmap: tombstones + clear frees owned keys ---- */
        strmap ecc_smap = strmap();
        char ecc_sk[16];
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 8; ecc_i++) {
            snprintf(ecc_sk, sizeof(ecc_sk), "a%d", ecc_i);
            if (ecc_smap.put(ecc_sk, ecc_i) != 0) ecc_good = 0;
        }
        ecc_smap.remove("a0");
        ecc_smap.remove("a4");
        ecc_check("ecc_smap_tombstones", ecc_good == 1 && ecc_smap.size() == 6 &&
                  ecc_smap.contains("a0") == 0 && ecc_smap.contains("a1") == 1);
        ecc_smap.clear();
        ecc_smap.clear();
        ecc_smap.clear();
        ecc_check("ecc_smap_clear_size", ecc_smap.size() == 0 &&
                  ecc_smap.contains("a1") == 0);
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 16; ecc_i++) {
            snprintf(ecc_sk, sizeof(ecc_sk), "b%d", ecc_i);
            if (ecc_smap.put(ecc_sk, ecc_i + 1) != 0) ecc_good = 0;
        }
        for (ecc_i = 0; ecc_i < 16; ecc_i++) {
            snprintf(ecc_sk, sizeof(ecc_sk), "b%d", ecc_i);
            ecc_v = -1;
            if (ecc_smap.get(ecc_sk, &ecc_v) != 0 || ecc_v != ecc_i + 1)
                ecc_good = 0;
        }
        ecc_check("ecc_smap_rebuild_after_clear",
                  ecc_good == 1 && ecc_smap.size() == 16);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_smap.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_smap_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.ownership_violations == 0);

        /* ---------------- strmap: oversized key guard (limit override) --- */
        if (ecc_limit > 2u && ecc_limit + 1u < sizeof(ecc_bigkey)) {
            strmap ecc_osmap = strmap();
            int ecc_os_ok = 1;
            memset(ecc_bigkey, 'k', ecc_limit);
            ecc_bigkey[ecc_limit] = '\0';

            /* length limit-1 is the largest key the allocator may copy */
            ecc_bigkey[ecc_limit - 1u] = '\0';
            if (ecc_osmap.put(ecc_bigkey, 11) != 0) ecc_os_ok = 0;
            ecc_bigkey[ecc_limit - 1u] = 'k';
            ecc_check("ecc_strmap_limit_minus_one",
                      ecc_os_ok == 1 && ecc_osmap.size() == 1);

            /* length == limit and limit+1 must be rejected before alloc */
            ecc_bigkey[ecc_limit] = '\0';
            stdlib_telemetry_snapshot(&ecc_pre);
            ecc_rc = ecc_osmap.put(ecc_bigkey, 12);
            stdlib_telemetry_snapshot(&ecc_post);
            ecc_check("ecc_strmap_oversized_key_rejected",
                      ecc_rc == -1 && ecc_osmap.size() == 1);
            ecc_check("ecc_strmap_oversized_key_no_alloc",
                      ecc_post.alloc_count == ecc_pre.alloc_count &&
                      ecc_post.free_count == ecc_pre.free_count);

            ecc_bigkey[ecc_limit + 1u] = '\0';
            ecc_rc = ecc_osmap.put(ecc_bigkey, 13);
            ecc_bigkey[ecc_limit] = '\0';
            ecc_v = -1;
            ecc_check("ecc_strmap_overlimit_key_rejected",
                      ecc_rc == -1 && ecc_osmap.size() == 1 &&
                      ecc_osmap.get(ecc_bigkey, &ecc_v) == -1);
            ecc_bigkey[ecc_limit - 1u] = '\0';
            ecc_v = -1;
            ecc_check("ecc_strmap_oversized_atomic",
                      ecc_osmap.get(ecc_bigkey, &ecc_v) == 0 && ecc_v == 11 &&
                      ecc_osmap.contains("missing") == 0);
            stdlib_telemetry_snapshot(&ecc_pre);
            ecc_osmap.clear(); /* must release the owned 255-byte key */
            stdlib_telemetry_snapshot(&ecc_post);
            ecc_check("ecc_strmap_clear_frees_key",
                      ecc_post.free_count > ecc_pre.free_count &&
                      ecc_post.alloc_count == ecc_pre.alloc_count &&
                      ecc_post.ownership_violations == 0);
            stdlib_telemetry_snapshot(&ecc_pre);
            ecc_osmap.drop();
            stdlib_telemetry_snapshot(&ecc_post);
            ecc_check("ecc_strmap_osmap_drop_balance",
                      ecc_post.free_count - ecc_pre.free_count >= 3 &&
                      ecc_post.alloc_count == ecc_pre.alloc_count);
        }

        /* ---------------- doubling-overflow guards ----------------------- */
        deque ecc_gdeq = deque();
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 64; ecc_i++)
            if (ecc_gdeq.push_back(ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_deq_fill_to_limit",
                  ecc_good == 1 && ecc_gdeq.size() == 64 &&
                  ecc_gdeq.capacity() == 64);
        ecc_sz = ecc_gdeq.size();
        ecc_cap = ecc_gdeq.capacity();
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_rc = ecc_gdeq.push_back(999);
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_deq_doubling_guard",
                  ecc_rc == -1 && ecc_gdeq.size() == ecc_sz &&
                  ecc_gdeq.capacity() == ecc_cap &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.free_count == ecc_pre.free_count);
        ecc_check("ecc_deq_guard_recovers",
                  ecc_gdeq.pop_front(&ecc_v) == 0 && ecc_v == 0 &&
                  ecc_gdeq.push_back(999) == 0 && ecc_gdeq.size() == 64 &&
                  ecc_gdeq.at_checked(63, &ecc_v) == 0 && ecc_v == 999);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_gdeq.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_gdeq_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count);

        stack ecc_gstk = stack();
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 64; ecc_i++)
            if (ecc_gstk.push(ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_stack_fill_to_limit",
                  ecc_good == 1 && ecc_gstk.size() == 64);
        ecc_sz = ecc_gstk.size();
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_rc = ecc_gstk.push(999);
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_stack_doubling_guard",
                  ecc_rc == -1 && ecc_gstk.size() == ecc_sz &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.free_count == ecc_pre.free_count);
        ecc_check("ecc_stack_guard_recovers",
                  ecc_gstk.pop(&ecc_v) == 0 && ecc_v == 63 &&
                  ecc_gstk.push(999) == 0 && ecc_gstk.size() == 64 &&
                  ecc_gstk.top_checked(&ecc_v) == 0 && ecc_v == 999);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_gstk.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_gstk_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count);

        queue ecc_gque = queue();
        ecc_good = 1;
        for (ecc_i = 0; ecc_i < 64; ecc_i++)
            if (ecc_gque.push(ecc_i) != 0) ecc_good = 0;
        ecc_check("ecc_queue_fill_to_limit",
                  ecc_good == 1 && ecc_gque.size() == 64);
        ecc_sz = ecc_gque.size();
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_rc = ecc_gque.push(999);
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_queue_doubling_guard",
                  ecc_rc == -1 && ecc_gque.size() == ecc_sz &&
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.free_count == ecc_pre.free_count);
        ecc_check("ecc_queue_guard_recovers",
                  ecc_gque.pop(&ecc_v) == 0 && ecc_v == 0 &&
                  ecc_gque.push(999) == 0 && ecc_gque.size() == 64 &&
                  ecc_gque.front_checked(&ecc_v) == 0 && ecc_v == 1);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_gque.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_gque_drop_balance",
                  ecc_post.free_count > ecc_pre.free_count &&
                  ecc_post.alloc_count == ecc_pre.alloc_count);

        /* ---------------- rebuild-capacity overflow + grow(SIZE_MAX) ----- */
        {
            size_t ecc_ov_cap = (size_t)-2; /* even, > SIZE_MAX >> 2 */
            size_t ecc_ov_cnt = ecc_ov_cap / 2u - 1u;
            ecc_check("ecc_rebuild_capacity_overflow",
                      stdlib_map_rebuild_capacity(ecc_ov_cnt, ecc_ov_cap) == 0u);
            ecc_check("ecc_rebuild_capacity_double",
                      stdlib_map_rebuild_capacity(3u, 8u) == 16u);
            ecc_check("ecc_rebuild_capacity_same",
                      stdlib_map_rebuild_capacity(2u, 16u) == 16u);
        }
        intmap ecc_gmap = intmap();
        strmap ecc_gsmap = strmap();
        intset ecc_gset = intset();
        ecc_gmap.put(1, 1);
        ecc_gsmap.put("g", 1);
        ecc_gset.insert(1);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_rc = ecc_gmap.grow((size_t)-1);
        ecc_check("ecc_intmap_grow_max_rejected",
                  ecc_rc == -1 && ecc_gmap.size() == 1 &&
                  ecc_gmap.contains(1) == 1);
        ecc_rc = ecc_gsmap.grow((size_t)-1);
        ecc_check("ecc_strmap_grow_max_rejected",
                  ecc_rc == -1 && ecc_gsmap.size() == 1 &&
                  ecc_gsmap.contains("g") == 1);
        ecc_rc = ecc_gset.grow((size_t)-1);
        ecc_check("ecc_intset_grow_max_rejected",
                  ecc_rc == -1 && ecc_gset.size() == 1 &&
                  ecc_gset.contains(1) == 1);
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_grow_max_no_alloc",
                  ecc_post.alloc_count == ecc_pre.alloc_count &&
                  ecc_post.free_count == ecc_pre.free_count);
        stdlib_telemetry_snapshot(&ecc_pre);
        ecc_gmap.drop();
        ecc_gsmap.drop();
        ecc_gset.drop();
        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_grow_containers_drop_balance",
                  ecc_post.free_count - ecc_pre.free_count >= 6 &&
                  ecc_post.alloc_count == ecc_pre.alloc_count);

        stdlib_telemetry_snapshot(&ecc_post);
        ecc_check("ecc_global_balance",
                  ecc_post.alloc_count == ecc_post.free_count &&
                  ecc_post.ownership_violations == 0 &&
                  ecc_post.lifecycle_failures == 0);

        ecc_final_rc = ecc_failures == 0 ? 0 : 1;
    }

    stdlib_telemetry_set_enabled(0);
    print("EXTRA_COLLECTIONS_CLEAR", ecc_checks, ecc_failures);
    return ecc_final_rc;
}
