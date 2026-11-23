/* Container-safety regression suite -- basic shape, growth/shrink, removal,
 * duplicate/missing keys and capacity-overflow rejection.
 *
 * SELF-CHECKING: each check prints "<tag> <1|0>" (1 = passed, 0 = failed).
 * The final line is "CONTAINER_SAFETY <checks> <failures>".
 * Exit code 0 == no failures, 1 == at least one failure.
 *
 * All loops are bounded and all data is fixed: the program terminates quickly
 * under an external timeout, and reaching the final line proves no probing or
 * growth loop hung.  No stale pointer is ever dereferenced; invalidation is
 * observed only through generation/validity metadata (see cs_generation.cp).
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

static int g_checks = 0;
static int g_failed = 0;

static void check(const char *tag, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(tag, cond ? 1 : 0);
}

int main(void) {
    int out = 0;
    int i;

    /* ---- vector: empty, single, grow, reserve, shrink, overflow ---------- */
    {
        vector cs_vec_a = vector();
        check("b_vec_empty", cs_vec_a.size() == 0 && cs_vec_a.capacity() == 0 &&
                             cs_vec_a.at_checked(0, &out) == -1 &&
                             cs_vec_a.pop_back(&out) == -1 &&
                             cs_vec_a.erase(0, &out) == -1);
        check("b_vec_single", cs_vec_a.push_back(7) == 0 && cs_vec_a.size() == 1 &&
                              cs_vec_a.at_checked(0, &out) == 0 && out == 7);
        for (i = 1; i < 64; i++)
            if (cs_vec_a.push_back(i * 3) != 0)
                break;
        check("b_vec_grow", cs_vec_a.size() == 64 && cs_vec_a.capacity() >= 64 &&
                            cs_vec_a.at_checked(63, &out) == 0 && out == 63 * 3);
        check("b_vec_reserve", cs_vec_a.reserve(128) == 0 &&
                               cs_vec_a.capacity() >= 128 && cs_vec_a.size() == 64 &&
                               cs_vec_a.at_checked(0, &out) == 0 && out == 7);
        check("b_vec_shrink", cs_vec_a.shrink_to_fit() == 0 &&
                              cs_vec_a.capacity() == 64 && cs_vec_a.size() == 64 &&
                              cs_vec_a.at_checked(63, &out) == 0 && out == 63 * 3);
        check("b_vec_overflow", cs_vec_a.reserve(SIZE_MAX) == -1 &&
                                cs_vec_a.capacity() == 64 && cs_vec_a.size() == 64 &&
                                cs_vec_a.at_checked(0, &out) == 0 && out == 7);
        cs_vec_a.drop();
        cs_vec_a.drop();
        check("b_vec_drop2", cs_vec_a.size() == 0 && cs_vec_a.capacity() == 0);
    }

    /* ---- vector: insert/erase at begin, middle, end and out of range ----- */
    {
        vector cs_vec_b = vector();
        for (i = 0; i < 8; i++)
            cs_vec_b.push_back(i);
        check("b_vec_ins_begin", cs_vec_b.insert(0, 100) == 0 &&
                                 cs_vec_b.size() == 9 &&
                                 cs_vec_b.at_checked(0, &out) == 0 && out == 100 &&
                                 cs_vec_b.at_checked(1, &out) == 0 && out == 0);
        check("b_vec_ins_mid", cs_vec_b.insert(5, 200) == 0 &&
                               cs_vec_b.size() == 10 &&
                               cs_vec_b.at_checked(5, &out) == 0 && out == 200 &&
                               cs_vec_b.at_checked(6, &out) == 0 && out == 4);
        check("b_vec_ins_end", cs_vec_b.insert(cs_vec_b.size(), 300) == 0 &&
                               cs_vec_b.size() == 11 &&
                               cs_vec_b.at_checked(10, &out) == 0 && out == 300);
        check("b_vec_ins_oob", cs_vec_b.insert(99, 1) == -1 && cs_vec_b.size() == 11);
        check("b_vec_era_begin", cs_vec_b.erase(0, &out) == 0 && out == 100 &&
                                 cs_vec_b.size() == 10 &&
                                 cs_vec_b.at_checked(0, &out) == 0 && out == 0);
        /* After erase(0) slot 5 holds 4 (200 moved to slot 4). */
        check("b_vec_era_mid", cs_vec_b.erase(5, &out) == 0 && out == 4 &&
                               cs_vec_b.size() == 9 &&
                               cs_vec_b.at_checked(5, &out) == 0 && out == 5);
        check("b_vec_era_end", cs_vec_b.erase(cs_vec_b.size() - 1, &out) == 0 &&
                               out == 300 && cs_vec_b.size() == 8 &&
                               cs_vec_b.at_checked(7, &out) == 0 && out == 7);
        check("b_vec_era_oob", cs_vec_b.erase(99, &out) == -1 && cs_vec_b.size() == 8);
        cs_vec_b.drop();
    }

    /* ---- intmap: empty, duplicate/missing keys, grow overflow, clear ---- */
    {
        intmap cs_map_a = intmap();
        check("b_map_empty", cs_map_a.size() == 0 && cs_map_a.valid() == 0 &&
                             cs_map_a.get(1, &out) == -1 &&
                             cs_map_a.contains(1) == 0 &&
                             cs_map_a.remove(1) == -1);
        check("b_map_put", cs_map_a.put(10, 100) == 0 && cs_map_a.size() == 1 &&
                           cs_map_a.valid() == 1 &&
                           cs_map_a.get(10, &out) == 0 && out == 100);
        check("b_map_dup", cs_map_a.put(10, 200) == 0 && cs_map_a.size() == 1 &&
                           cs_map_a.get(10, &out) == 0 && out == 200);
        check("b_map_missing", cs_map_a.get(999, &out) == -1 &&
                               cs_map_a.contains(999) == 0 &&
                               cs_map_a.remove(999) == -1 && cs_map_a.size() == 1);
        check("b_map_grow_ovf", cs_map_a.grow(SIZE_MAX) == -1 &&
                                cs_map_a.size() == 1 &&
                                cs_map_a.get(10, &out) == 0 && out == 200);
        check("b_map_pow2_ovf", stdlib_map_next_pow2(SIZE_MAX) == 0);
        for (i = 0; i < 300; i++)
            if (cs_map_a.put(i + 1000, i) != 0)
                break;
        check("b_map_grow", cs_map_a.size() == 301 &&
                            cs_map_a.get(1299, &out) == 0 && out == 299);
        check("b_map_noop_grow", cs_map_a.grow(16) == 0 && cs_map_a.size() == 301);
        cs_map_a.clear();
        check("b_map_clear", cs_map_a.size() == 0 && cs_map_a.valid() == 1 &&
                             cs_map_a.get(10, &out) == -1);
        cs_map_a.drop();
        cs_map_a.drop();
        check("b_map_drop2", cs_map_a.size() == 0 && cs_map_a.valid() == 0);
    }

    /* ---- strmap: NULL/empty keys, duplicates, missing, overflow ---------- */
    {
        strmap cs_smap_a = strmap();
        char cs_key[24];
        check("b_smap_empty", cs_smap_a.size() == 0 && cs_smap_a.valid() == 0 &&
                              cs_smap_a.get("x", &out) == -1 &&
                              cs_smap_a.contains("x") == 0);
        check("b_smap_null", cs_smap_a.put(NULL, 1) == -1 && cs_smap_a.size() == 0 &&
                             cs_smap_a.get(NULL, &out) == -1 &&
                             cs_smap_a.remove(NULL) == -1);
        check("b_smap_emptykey", cs_smap_a.put("", 7) == 0 && cs_smap_a.size() == 1 &&
                                 cs_smap_a.get("", &out) == 0 && out == 7);
        check("b_smap_dup", cs_smap_a.put("", 8) == 0 && cs_smap_a.size() == 1 &&
                            cs_smap_a.get("", &out) == 0 && out == 8);
        check("b_smap_missing", cs_smap_a.get("nope", &out) == -1 &&
                                cs_smap_a.remove("nope") == -1 && cs_smap_a.size() == 1);
        check("b_smap_grow_ovf", cs_smap_a.grow(SIZE_MAX) == -1 &&
                                 cs_smap_a.size() == 1 &&
                                 cs_smap_a.get("", &out) == 0 && out == 8);
        for (i = 0; i < 400; i++) {
            snprintf(cs_key, sizeof(cs_key), "k%d", i);
            if (cs_smap_a.put(cs_key, i) != 0)
                break;
        }
        check("b_smap_grow", cs_smap_a.size() == 401);
        snprintf(cs_key, sizeof(cs_key), "k%d", 399);
        check("b_smap_probe", cs_smap_a.get(cs_key, &out) == 0 && out == 399);
        cs_smap_a.remove("");
        check("b_smap_remove", cs_smap_a.size() == 400 &&
                               cs_smap_a.contains("") == 0);
        cs_smap_a.drop();
        cs_smap_a.drop();
        check("b_smap_drop2", cs_smap_a.size() == 0 && cs_smap_a.valid() == 0);
    }

    /* ---- intset: empty, duplicates, missing, overflow --------------------- */
    {
        intset cs_set_a = intset();
        check("b_set_empty", cs_set_a.size() == 0 && cs_set_a.valid() == 0 &&
                             cs_set_a.contains(5) == 0 && cs_set_a.remove(5) == -1);
        check("b_set_insert", cs_set_a.insert(5) == 0 && cs_set_a.size() == 1 &&
                              cs_set_a.valid() == 1 && cs_set_a.contains(5) == 1);
        check("b_set_dup", cs_set_a.insert(5) == 0 && cs_set_a.size() == 1);
        check("b_set_missing", cs_set_a.contains(999) == 0 && cs_set_a.remove(999) == -1 &&
                               cs_set_a.size() == 1);
        check("b_set_grow_ovf", cs_set_a.grow(SIZE_MAX) == -1 && cs_set_a.size() == 1 &&
                                cs_set_a.contains(5) == 1);
        for (i = 0; i < 300; i++)
            if (cs_set_a.insert(i * 7) != 0)
                break;
        /* Key 5 is distinct from the 300 multiples of 7 just inserted. */
        check("b_set_grow", cs_set_a.size() == 301 && cs_set_a.contains(7 * 299) == 1);
        cs_set_a.remove(5);
        check("b_set_remove", cs_set_a.size() == 300 && cs_set_a.contains(5) == 0);
        cs_set_a.clear();
        check("b_set_clear", cs_set_a.size() == 0 && cs_set_a.valid() == 1 &&
                             cs_set_a.contains(7 * 299) == 0);
        cs_set_a.drop();
        cs_set_a.drop();
        check("b_set_drop2", cs_set_a.size() == 0 && cs_set_a.valid() == 0);
    }

    /* ---- deque: empty/single, ring wrap, removal front/back, overflow ---- */
    {
        deque cs_deq_a = deque();
        check("b_deq_empty", cs_deq_a.size() == 0 && cs_deq_a.capacity() == 0 &&
                             cs_deq_a.pop_front(&out) == -1 &&
                             cs_deq_a.pop_back(&out) == -1 &&
                             cs_deq_a.at_checked(0, &out) == -1);
        check("b_deq_single", cs_deq_a.push_back(11) == 0 && cs_deq_a.size() == 1 &&
                              cs_deq_a.at_checked(0, &out) == 0 && out == 11);
        check("b_deq_front", cs_deq_a.push_front(22) == 0 && cs_deq_a.size() == 2 &&
                             cs_deq_a.at_checked(0, &out) == 0 && out == 22 &&
                             cs_deq_a.at_checked(1, &out) == 0 && out == 11);
        for (i = 0; i < 16; i++)
            cs_deq_a.push_back(100 + i);
        check("b_deq_grow", cs_deq_a.size() == 18 && cs_deq_a.capacity() >= 18 &&
                            cs_deq_a.at_checked(17, &out) == 0 && out == 115);
        check("b_deq_pop_front", cs_deq_a.pop_front(&out) == 0 && out == 22 &&
                                 cs_deq_a.size() == 17 &&
                                 cs_deq_a.at_checked(0, &out) == 0 && out == 11);
        check("b_deq_pop_back", cs_deq_a.pop_back(&out) == 0 && out == 115 &&
                                cs_deq_a.size() == 16 &&
                                cs_deq_a.at_checked(15, &out) == 0 && out == 114);
        check("b_deq_overflow", cs_deq_a.reserve(SIZE_MAX) == -1 &&
                                cs_deq_a.size() == 16);
        cs_deq_a.clear();
        check("b_deq_clear", cs_deq_a.size() == 0 && cs_deq_a.pop_front(&out) == -1);
        cs_deq_a.drop();
        cs_deq_a.drop();
        check("b_deq_drop2", cs_deq_a.size() == 0 && cs_deq_a.capacity() == 0);
    }

    /* ---- list: empty/single, removal front/back, middle read ------------- */
    {
        list cs_list_a = list();
        check("b_list_empty", cs_list_a.size() == 0 &&
                              cs_list_a.pop_front(&out) == -1 &&
                              cs_list_a.pop_back(&out) == -1 &&
                              cs_list_a.at_checked(0, &out) == -1);
        check("b_list_single", cs_list_a.push_back(33) == 0 && cs_list_a.size() == 1 &&
                               cs_list_a.at_checked(0, &out) == 0 && out == 33);
        check("b_list_front", cs_list_a.push_front(44) == 0 && cs_list_a.size() == 2 &&
                              cs_list_a.at_checked(0, &out) == 0 && out == 44);
        for (i = 0; i < 32; i++)
            cs_list_a.push_back(i);
        /* Content is [44,33,0,1,...]; index 17 is the inserted value 15. */
        check("b_list_grow", cs_list_a.size() == 34 &&
                             cs_list_a.at_checked(17, &out) == 0 && out == 15);
        check("b_list_pop_front", cs_list_a.pop_front(&out) == 0 && out == 44 &&
                                  cs_list_a.size() == 33 &&
                                  cs_list_a.at_checked(0, &out) == 0 && out == 33);
        check("b_list_pop_back", cs_list_a.pop_back(&out) == 0 && out == 31 &&
                                 cs_list_a.size() == 32 &&
                                 cs_list_a.at_checked(16, &out) == 0 && out == 15);
        cs_list_a.clear();
        check("b_list_clear", cs_list_a.size() == 0 && cs_list_a.at_checked(0, &out) == -1);
        cs_list_a.drop();
        cs_list_a.drop();
        check("b_list_drop2", cs_list_a.size() == 0);
    }

    /* ---- stack / queue: empty/single, LIFO/FIFO removal ------------------ */
    {
        stack cs_stack_a = stack();
        queue cs_queue_a = queue();
        check("b_stack_empty", cs_stack_a.size() == 0 &&
                               cs_stack_a.pop(&out) == -1 &&
                               cs_stack_a.top_checked(&out) == -1);
        check("b_stack_lifo", cs_stack_a.push(1) == 0 && cs_stack_a.push(2) == 0 &&
                              cs_stack_a.push(3) == 0 && cs_stack_a.size() == 3 &&
                              cs_stack_a.top_checked(&out) == 0 && out == 3 &&
                              cs_stack_a.pop(&out) == 0 && out == 3 &&
                              cs_stack_a.pop(&out) == 0 && out == 2 &&
                              cs_stack_a.pop(&out) == 0 && out == 1 &&
                              cs_stack_a.pop(&out) == -1);
        check("b_queue_empty", cs_queue_a.size() == 0 && cs_queue_a.pop(&out) == -1 &&
                               cs_queue_a.front_checked(&out) == -1);
        check("b_queue_fifo", cs_queue_a.push(1) == 0 && cs_queue_a.push(2) == 0 &&
                              cs_queue_a.push(3) == 0 && cs_queue_a.size() == 3 &&
                              cs_queue_a.front_checked(&out) == 0 && out == 1 &&
                              cs_queue_a.pop(&out) == 0 && out == 1 &&
                              cs_queue_a.pop(&out) == 0 && out == 2 &&
                              cs_queue_a.pop(&out) == 0 && out == 3 &&
                              cs_queue_a.pop(&out) == -1);
        cs_stack_a.clear();
        cs_queue_a.clear();
        cs_stack_a.drop();
        cs_stack_a.drop();
        cs_queue_a.drop();
        cs_queue_a.drop();
        check("b_sq_drop2", cs_stack_a.size() == 0 && cs_queue_a.size() == 0);
    }

    check("b_terminated", 1);
    print("CONTAINER_SAFETY", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
