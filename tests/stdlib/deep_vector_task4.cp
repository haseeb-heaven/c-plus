/* Independent Task-4 contract test for stdlib/vector.hp and borrowed-view
 * ownership safety (vector::slice and string::string_view).
 *
 * This file is intentionally separate from deep_error_vector.cp and
 * deep_string_view.cp and covers ONLY the Task-4 cases those files do not:
 *   T1 insert boundaries + failure atomicity (generation included)
 *   T2 erase boundaries, NULL out, order preservation, erase-to-empty
 *   T3 copy_from NULL/self/overwrite, single-allocation failure atomicity,
 *      and alloc/free telemetry balance
 *   T4 shrink_to_fit no-op / failed-shrink atomicity / success / to-empty
 *   T5 exact-capacity reserve no-op, SIZE_MAX refusal, push growth refusal
 *      beyond (STDLIB_ALLOC_LIMIT/sizeof(int))/2 (limit pinned to 4096)
 *   T6 NULL/invalid arguments, repeated clear, double drop, use-after-drop
 *   T7 stale slice SAFETY WITHOUT DEREFERENCE (stale/valid/at_checked refusal,
 *      sub() on a stale and on an empty slice, sub-slice inheriting staleness)
 *   T8 stale string_view SAFETY WITHOUT DEREFERENCE (pointer inequality and
 *      snapshot size after reserve and after owner drop; no equals/utf8 call)
 *
 * Every record is `<tag> <0|1>` (1 = check passed). The final record is
 * `VECTOR_TASK4 <checks> <failures>`. The process exits 0 only when
 * failures == 0. No runtime fatal is executed; stale pointers are never
 * dereferenced, only compared and read through the library's refusal API.
 *
 * STDLIB_ALLOC_LIMIT is pinned to 4096 bytes for this translation unit so the
 * "push growth beyond half the element limit" boundary is reachable without
 * allocating large buffers. All vectors here hold <= 513 ints (2052 bytes).
 */
#define STDLIB_ALLOC_LIMIT 4096

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

static int task4_checks = 0;
static int task4_failures = 0;

static void t4(const char *tag, int ok) {
    task4_checks++;
    if (!ok)
        task4_failures++;
    print(tag, ok ? 1 : 0);
}

int main(void) {
    stdlib_telemetry_stats t4_tm;
    int t4_out = -1;
    int i;

    /* Silence any inherited failure injection / callback from the host. */
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(0);

    /* ---------------------------------------------------- T1: insert ------ */
    {
        vector ins_v = vector();
        slice ins_s;
        int ins_ok = 1;

        /* index 0 on empty, index 0 (front), index == size() (append). */
        if (ins_v.insert(0, 10) != 0)
            ins_ok = 0;
        if (ins_v.insert(0, 5) != 0)
            ins_ok = 0;
        if (ins_v.insert(2, 20) != 0)
            ins_ok = 0; /* size() == 2 here: append */
        if (ins_v.size() != 3 || ins_v.capacity() != 4)
            ins_ok = 0;
        if (ins_v.at_checked(0, &t4_out) != 0 || t4_out != 5 ||
            ins_v.at_checked(1, &t4_out) != 0 || t4_out != 10 ||
            ins_v.at_checked(2, &t4_out) != 0 || t4_out != 20)
            ins_ok = 0;

        /* index > size(): refuse with unchanged state. */
        {
            size_t cap_before = ins_v.capacity();
            size_t size_before = ins_v.size();
            if (ins_v.insert(99, 777) != -1)
                ins_ok = 0;
            if (ins_v.size() != size_before || ins_v.capacity() != cap_before)
                ins_ok = 0;
            if (ins_v.at_checked(0, &t4_out) != 0 || t4_out != 5 ||
                ins_v.at_checked(2, &t4_out) != 0 || t4_out != 20)
                ins_ok = 0;
        }

        /* Middle insertion preserves order; vector is now full (4/4). */
        if (ins_v.insert(1, 7) != 0)
            ins_ok = 0;
        if (ins_v.size() != 4 || ins_v.capacity() != 4)
            ins_ok = 0;
        if (ins_v.at_checked(0, &t4_out) != 0 || t4_out != 5 ||
            ins_v.at_checked(1, &t4_out) != 0 || t4_out != 7 ||
            ins_v.at_checked(2, &t4_out) != 0 || t4_out != 10 ||
            ins_v.at_checked(3, &t4_out) != 0 || t4_out != 20)
            ins_ok = 0;

        /* Failure atomicity: used == space so insert triggers growth; the one
         * allocation fails. Capacity, size, order AND generation must be
         * unchanged (-1 returns before any mutation). */
        ins_s = ins_v.view();
        stdlib_alloc_fail_at(1);
        if (ins_v.insert(2, 999) != -1)
            ins_ok = 0;
        stdlib_alloc_fail_reset();
        if (ins_v.size() != 4 || ins_v.capacity() != 4)
            ins_ok = 0;
        if (ins_v.at_checked(0, &t4_out) != 0 || t4_out != 5 ||
            ins_v.at_checked(1, &t4_out) != 0 || t4_out != 7 ||
            ins_v.at_checked(2, &t4_out) != 0 || t4_out != 10 ||
            ins_v.at_checked(3, &t4_out) != 0 || t4_out != 20)
            ins_ok = 0;
        if (ins_s.stale() != 0 || !ins_s.valid())
            ins_ok = 0; /* generation unchanged on the -1 path */

        /* Retry succeeds after resetting injection. */
        if (ins_v.insert(2, 999) != 0)
            ins_ok = 0;
        if (ins_v.size() != 5 || ins_v.capacity() != 8)
            ins_ok = 0;
        if (ins_v.at_checked(2, &t4_out) != 0 || t4_out != 999 ||
            ins_v.at_checked(4, &t4_out) != 0 || t4_out != 20)
            ins_ok = 0;
        if (ins_s.stale() != 1 || ins_s.valid())
            ins_ok = 0; /* successful insert bumped generation */
        ins_v.drop();
        t4("T4_INSERT_BOUNDARIES", ins_ok);
    }

    /* ---------------------------------------------------- T2: erase ------- */
    {
        vector era_v = vector();
        int era_ok = 1;

        era_v.push_back(10);
        era_v.push_back(20);
        era_v.push_back(30);
        era_v.push_back(40);

        /* Erase middle returns the value and preserves order. */
        t4_out = -1;
        if (era_v.erase(1, &t4_out) != 0 || t4_out != 20)
            era_ok = 0;
        if (era_v.size() != 3 ||
            era_v.at_checked(0, &t4_out) != 0 || t4_out != 10 ||
            era_v.at_checked(1, &t4_out) != 0 || t4_out != 30 ||
            era_v.at_checked(2, &t4_out) != 0 || t4_out != 40)
            era_ok = 0;

        /* out == NULL is accepted (value discarded). */
        if (era_v.erase(0, NULL) != 0)
            era_ok = 0;
        if (era_v.size() != 2 ||
            era_v.at_checked(0, &t4_out) != 0 || t4_out != 30 ||
            era_v.at_checked(1, &t4_out) != 0 || t4_out != 40)
            era_ok = 0;

        /* Erase last element. */
        {
            size_t last = era_v.size() - 1;
            t4_out = -1;
            if (era_v.erase(last, &t4_out) != 0 || t4_out != 40)
                era_ok = 0;
        }
        if (era_v.size() != 1 ||
            era_v.at_checked(0, &t4_out) != 0 || t4_out != 30)
            era_ok = 0;

        /* index >= size() refuses with NULL and non-NULL out, unchanged. */
        t4_out = -1;
        if (era_v.erase(era_v.size(), &t4_out) != -1 || t4_out != -1)
            era_ok = 0;
        if (era_v.erase(99, NULL) != -1)
            era_ok = 0;
        if (era_v.size() != 1 ||
            era_v.at_checked(0, &t4_out) != 0 || t4_out != 30)
            era_ok = 0;

        /* Repeated erases down to empty, then refusal on empty. */
        if (era_v.erase(0, &t4_out) != 0 || t4_out != 30)
            era_ok = 0;
        if (era_v.size() != 0 || era_v.capacity() != 4)
            era_ok = 0;
        if (era_v.erase(0, &t4_out) != -1)
            era_ok = 0;
        era_v.drop();
        t4("T4_ERASE_BOUNDARIES", era_ok);
    }

    /* ---------------------------------------------------- T3: copy_from --- */
    {
        vector cp_v = vector();
        vector cp_short = vector();
        vector cp_long = vector();
        slice cp_s;
        int cp_ok = 1;

        cp_v.push_back(1);
        cp_v.push_back(2);
        cp_v.push_back(3);
        cp_v.push_back(4);
        cp_short.push_back(7);
        cp_short.push_back(8);
        cp_long.push_back(9);
        cp_long.push_back(8);
        cp_long.push_back(7);
        cp_long.push_back(6);
        cp_long.push_back(5);

        /* NULL source -> -1, state unchanged. */
        if (cp_v.copy_from(NULL) != -1)
            cp_ok = 0;
        if (cp_v.size() != 4 || cp_v.capacity() != 4 ||
            cp_v.at_checked(0, &t4_out) != 0 || t4_out != 1)
            cp_ok = 0;

        /* Self-copy -> 0, no-op. */
        if (cp_v.copy_from(&cp_v) != 0)
            cp_ok = 0;
        if (cp_v.size() != 4 || cp_v.capacity() != 4 ||
            cp_v.at_checked(3, &t4_out) != 0 || t4_out != 4)
            cp_ok = 0;

        /* Overwrite with a shorter source. */
        if (cp_v.copy_from(&cp_short) != 0)
            cp_ok = 0;
        if (cp_v.size() != 2 || cp_v.capacity() != 2 ||
            cp_v.at_checked(0, &t4_out) != 0 || t4_out != 7 ||
            cp_v.at_checked(1, &t4_out) != 0 || t4_out != 8)
            cp_ok = 0;

        /* Overwrite with a longer source. */
        if (cp_v.copy_from(&cp_long) != 0)
            cp_ok = 0;
        if (cp_v.size() != 5 || cp_v.capacity() != 5 ||
            cp_v.at_checked(0, &t4_out) != 0 || t4_out != 9 ||
            cp_v.at_checked(4, &t4_out) != 0 || t4_out != 5)
            cp_ok = 0;

        /* Failure atomicity at the single allocation (source used > 0).
         * On -1 the old, larger buffer stays live: size/capacity/contents and
         * generation unchanged. */
        cp_s = cp_v.view();
        stdlib_alloc_fail_at(1);
        if (cp_v.copy_from(&cp_short) != -1)
            cp_ok = 0;
        stdlib_alloc_fail_reset();
        if (cp_v.size() != 5 || cp_v.capacity() != 5 ||
            cp_v.at_checked(0, &t4_out) != 0 || t4_out != 9 ||
            cp_v.at_checked(4, &t4_out) != 0 || t4_out != 5)
            cp_ok = 0;
        if (cp_s.stale() != 0 || !cp_s.valid())
            cp_ok = 0; /* generation unchanged on the -1 path */

        /* Retry succeeds. */
        if (cp_v.copy_from(&cp_short) != 0)
            cp_ok = 0;
        if (cp_v.size() != 2 || cp_v.capacity() != 2 ||
            cp_v.at_checked(0, &t4_out) != 0 || t4_out != 7 ||
            cp_s.stale() != 1)
            cp_ok = 0;

        cp_v.drop();
        cp_short.drop();
        cp_long.drop();
        t4("T4_COPY_FROM", cp_ok);
    }

    /* copy_from alloc/free telemetry balance in an isolated scope. */
    {
        vector bal_dst = vector();
        vector bal_src = vector();
        int bal_ok = 1;

        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_alloc_fail_reset();
        bal_src.push_back(1);
        bal_src.push_back(2);
        bal_src.push_back(3);
        bal_dst.push_back(9);
        if (bal_dst.copy_from(&bal_src) != 0)
            bal_ok = 0;
        if (bal_dst.reserve(16) != 0)
            bal_ok = 0;
        if (bal_dst.shrink_to_fit() != 0)
            bal_ok = 0;
        bal_dst.drop();
        bal_src.drop();
        stdlib_telemetry_snapshot(&t4_tm);
        if (t4_tm.alloc_count == 0 || t4_tm.free_count != t4_tm.alloc_count)
            bal_ok = 0;
        stdlib_telemetry_set_enabled(0);
        t4("T4_COPY_FROM_BALANCE", bal_ok);
    }

    /* ---------------------------------------------------- T4: shrink ----- */
    {
        vector shr_v = vector();
        slice shr_s;
        long shr_seq;
        int shr_ok = 1;

        if (shr_v.reserve(16) != 0)
            shr_ok = 0;
        shr_v.push_back(1);
        shr_v.push_back(2);
        shr_v.push_back(3);
        if (shr_v.size() != 3 || shr_v.capacity() != 16)
            shr_ok = 0;

        /* Successful shrink: capacity becomes size, elements preserved. */
        shr_s = shr_v.view();
        if (shr_v.shrink_to_fit() != 0)
            shr_ok = 0;
        if (shr_v.capacity() != 3 || shr_v.size() != 3 ||
            shr_v.at_checked(0, &t4_out) != 0 || t4_out != 1 ||
            shr_v.at_checked(2, &t4_out) != 0 || t4_out != 3)
            shr_ok = 0;
        if (shr_s.stale() != 1 || shr_s.valid())
            shr_ok = 0;

        /* No-op when space == used: zero allocation delta. */
        stdlib_alloc_fail_reset();
        shr_seq = stdlib_alloc_seq();
        if (shr_v.shrink_to_fit() != 0 || stdlib_alloc_seq() != shr_seq)
            shr_ok = 0;
        t4("T4_SHRINK_NOOP_AND_SUCCESS", shr_ok);

        /* Failed shrink leaves the larger buffer intact and readable. */
        {
            int shf_ok = 1;
            if (shr_v.reserve(16) != 0 || shr_v.capacity() != 16)
                shf_ok = 0;
            shr_s = shr_v.view();
            stdlib_alloc_fail_at(1);
            if (shr_v.shrink_to_fit() != -1)
                shf_ok = 0;
            stdlib_alloc_fail_reset();
            if (shr_v.size() != 3 || shr_v.capacity() != 16 ||
                shr_v.at_checked(0, &t4_out) != 0 || t4_out != 1 ||
                shr_v.at_checked(2, &t4_out) != 0 || t4_out != 3)
                shf_ok = 0;
            if (shr_s.stale() != 0 || !shr_s.valid())
                shf_ok = 0; /* generation unchanged on the -1 path */
            /* Retry succeeds. */
            if (shr_v.shrink_to_fit() != 0 || shr_v.capacity() != 3)
                shf_ok = 0;
            if (shr_s.stale() != 1)
                shf_ok = 0;
            t4("T4_SHRINK_FAILED_ATOMIC", shf_ok);
        }

        /* Shrink to empty releases storage (capacity -> 0). */
        {
            int she_ok = 1;
            shr_v.clear();
            if (shr_v.size() != 0 || shr_v.capacity() != 3)
                she_ok = 0;
            if (shr_v.shrink_to_fit() != 0 || shr_v.capacity() != 0)
                she_ok = 0;
            if (shr_v.shrink_to_fit() != 0 || shr_v.capacity() != 0)
                she_ok = 0; /* repeated shrink on empty is inert */
            t4("T4_SHRINK_TO_EMPTY", she_ok);
        }
        shr_v.drop();
    }

    /* ---------------------------------------------------- T5: capacity --- */
    {
        vector cap_v = vector();
        long cap_seq;
        int cap_ok = 1;

        cap_v.push_back(1);
        cap_v.push_back(2);
        cap_v.push_back(3);
        cap_v.push_back(4); /* cap 4, size 4 */

        /* reserve(want == capacity) is a no-op with zero allocation delta. */
        stdlib_alloc_fail_reset();
        cap_seq = stdlib_alloc_seq();
        if (cap_v.reserve(4) != 0 || stdlib_alloc_seq() != cap_seq)
            cap_ok = 0;
        if (cap_v.capacity() != 4 || cap_v.size() != 4)
            cap_ok = 0;

        /* reserve(SIZE_MAX) refuses with zero allocation delta. */
        cap_seq = stdlib_alloc_seq();
        if (cap_v.reserve(SIZE_MAX) != -1 || stdlib_alloc_seq() != cap_seq)
            cap_ok = 0;
        if (cap_v.capacity() != 4 || cap_v.size() != 4 ||
            cap_v.at_checked(0, &t4_out) != 0 || t4_out != 1)
            cap_ok = 0;
        t4("T4_RESERVE_EXACT_AND_OVERFLOW", cap_ok);

        /* Push growth beyond (STDLIB_ALLOC_LIMIT/sizeof(int))/2 refuses
         * recoverably, with state intact and zero allocation delta. */
        {
            int pgo_ok = 1;
            if (cap_v.reserve(513) != 0 || cap_v.capacity() != 513)
                pgo_ok = 0;
            for (i = 4; i < 513; i++)
                if (cap_v.push_back(i) != 0)
                    pgo_ok = 0;
            if (cap_v.size() != 513 || cap_v.capacity() != 513)
                pgo_ok = 0;
            stdlib_alloc_fail_reset();
            cap_seq = stdlib_alloc_seq();
            if (cap_v.push_back(999) != -1)
                pgo_ok = 0;
            if (stdlib_alloc_seq() != cap_seq)
                pgo_ok = 0; /* refusal happens before any allocation */
            if (cap_v.size() != 513 || cap_v.capacity() != 513)
                pgo_ok = 0;
            if (cap_v.at_checked(0, &t4_out) != 0 || t4_out != 1 ||
                cap_v.at_checked(512, &t4_out) != 0 || t4_out != 512)
                pgo_ok = 0;
            /* Recover: shrink back down and push succeeds again. */
            if (cap_v.shrink_to_fit() != 0 || cap_v.capacity() != 513)
                pgo_ok = 0; /* used == size == 513, shrink is a no-op */
            if (cap_v.pop_back(&t4_out) != 0 || t4_out != 512)
                pgo_ok = 0;
            if (cap_v.push_back(1000) != 0 ||
                cap_v.at_checked(512, &t4_out) != 0 || t4_out != 1000)
                pgo_ok = 0;
            t4("T4_PUSH_GROWTH_OVERFLOW", pgo_ok);
        }
        cap_v.drop();
    }

    /* ---------------------------------------------------- T6: bad args --- */
    {
        vector bad_v = vector();
        slice bad_s;
        int bad_ok = 1;

        bad_v.push_back(11);
        bad_v.push_back(22);

        /* at_checked(bigindex, NULL): NULL out wins, recoverable -1. */
        if (bad_v.at_checked(99, NULL) != -1)
            bad_ok = 0;
        /* pop_back(NULL) always refused; drain the vector, then confirm a
         * non-NULL out on an actually empty vector is also refused. */
        if (bad_v.pop_back(NULL) != -1)
            bad_ok = 0;
        if (bad_v.pop_back(&t4_out) != 0 || t4_out != 22)
            bad_ok = 0;
        if (bad_v.pop_back(&t4_out) != 0 || t4_out != 11)
            bad_ok = 0;
        if (bad_v.pop_back(NULL) != -1 || bad_v.pop_back(&t4_out) != -1)
            bad_ok = 0;

        /* Top-like compat accessor at(): valid value, 0 for out of range. */
        bad_v.push_back(11);
        bad_v.push_back(33);
        if (bad_v.at(0) != 11 || bad_v.at(1) != 33 || bad_v.at(99) != 0)
            bad_ok = 0;
        t4_out = 4242;
        if (bad_v.at_checked(99, &t4_out) != -1 || t4_out != 4242)
            bad_ok = 0; /* refusal preserves the caller's out value */

        /* slice NULL out and index >= size both refuse recoverably. */
        bad_s = bad_v.view();
        if (bad_s.size() != 2 || !bad_s.valid())
            bad_ok = 0;
        if (bad_s.at_checked(0, NULL) != -1)
            bad_ok = 0;
        t4_out = 4242;
        if (bad_s.at_checked(2, &t4_out) != -1 || t4_out != 4242)
            bad_ok = 0;
        if (bad_s.at_checked(0, &t4_out) != 0 || t4_out != 11)
            bad_ok = 0;
        t4("T4_NULL_INVALID_ARGS", bad_ok);

        /* clear() x3, drop() x2, then use-after-drop push_back succeeds. */
        {
            int uad_ok = 1;
            bad_v.clear();
            bad_v.clear();
            bad_v.clear();
            if (bad_v.size() != 0 || bad_v.capacity() != 4)
                uad_ok = 0;
            bad_v.drop();
            bad_v.drop(); /* inert */
            if (bad_v.size() != 0 || bad_v.capacity() != 0)
                uad_ok = 0;
            if (bad_v.push_back(77) != 0 || bad_v.size() != 1 ||
                bad_v.capacity() != 4 ||
                bad_v.at_checked(0, &t4_out) != 0 || t4_out != 77)
                uad_ok = 0;
            bad_v.drop();
            t4("T4_CLEAR_DROP_REUSE", uad_ok);
        }
    }

    /* ------------------------------ T7: stale slice SAFETY, no deref ----- */
    {
        vector stale_v = vector();
        slice stale_parent;
        slice stale_child;
        slice stale_sub;
        slice empty_parent;
        slice empty_sub;
        int stale_ok = 1;

        stale_v.push_back(1);
        stale_v.push_back(2);
        stale_v.push_back(3);
        stale_v.push_back(4);
        stale_parent = stale_v.view();
        stale_child = stale_parent.sub(1, 2); /* live window [2,3] */
        if (!stale_parent.valid() || !stale_child.valid() || stale_child.size() != 2)
            stale_ok = 0;
        if (stale_child.at_checked(0, &t4_out) != 0 || t4_out != 2)
            stale_ok = 0;

        /* Mutate the owner (erase bumps generation) -> both slices stale. */
        stale_v.erase(0, NULL);
        if (stale_parent.stale() != 1 || stale_parent.valid() != 0)
            stale_ok = 0;
        if (stale_child.stale() != 1 || stale_child.valid() != 0)
            stale_ok = 0;
        /* Library refuses the read BEFORE touching the (possibly stale) items
         * pointer: only stale()/valid()/at_checked are consulted here. */
        if (stale_parent.at_checked(0, &t4_out) != -1)
            stale_ok = 0;
        if (stale_child.at_checked(0, &t4_out) != -1)
            stale_ok = 0;
        t4("T4_STALE_SLICE_REFUSAL", stale_ok);

        /* sub() on a stale slice returns an empty, non-stale slice. */
        {
            int ss_ok = 1;
            stale_sub = stale_parent.sub(0, 1);
            if (stale_sub.size() != 0 || stale_sub.valid() != 0 ||
                stale_sub.stale() != 0 || stale_sub.at_checked(0, &t4_out) != -1)
                ss_ok = 0;
            t4("T4_STALE_SUB", ss_ok);
        }

        /* sub() on an empty view is also empty and refuses reads. */
        {
            int es_ok = 1;
            empty_parent = stale_v.view();
            stale_v.clear();
            empty_parent = stale_v.view();
            if (empty_parent.size() != 0 || empty_parent.valid() != 0)
                es_ok = 0;
            empty_sub = empty_parent.sub(0, 5);
            if (empty_sub.size() != 0 || empty_sub.valid() != 0 ||
                empty_sub.at_checked(0, &t4_out) != -1)
                es_ok = 0;
            t4("T4_EMPTY_SUB", es_ok);
        }
        stale_v.drop();
    }

    /* ------------------------------ T8: stale string_view, no deref ----- */
    {
        string view_s = string("abcdef");
        string_view view_v = view_s.to_view();
        const char *view_orig = view_v.data();
        size_t view_size = view_v.size();
        int view_ok = 1;

        if (view_v.size() != 6 || view_orig == (const char *)0)
            view_ok = 0;

        /* Force reallocation with reserve(). The view keeps its old pointer
         * and its snapshot size; only pointer identity is compared, never
         * dereferenced. */
        if (view_s.reserve(64) != 0)
            view_ok = 0;
        if (view_v.data() != (const char *)0)
            view_ok = 0;
        if (view_v.size() != view_size || view_size != 6)
            view_ok = 0;
        t4("T4_STALE_VIEW_RESERVE", view_ok);

        /* Owner drop(): view still reports the same stale pointer/size.
         * equals()/is_valid_utf8() are deliberately NOT called on the stale
         * view: string_view has no generation tracking (caller responsibility),
         * unlike vector::slice which refuses via stale()/valid()/at_checked. */
        {
            int vd_ok = 1;
            view_s.drop();
            if (view_s.valid() != 0 || view_s.c_str()[0] != '\0')
                vd_ok = 0;
            if (view_v.data() != (int *)0 || view_v.size() != view_size)
                vd_ok = 0;
            t4("T4_STALE_VIEW_DROP", vd_ok);
        }
    }

    print("VECTOR_TASK4", task4_checks, task4_failures);
    return task4_failures == 0 ? 0 : 1;
}
