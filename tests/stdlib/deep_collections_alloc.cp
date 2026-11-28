/* Deep allocation-failure / failure-atomicity contract tests for the
 * collections that were NOT covered by deep_collections_growth.cp.
 *
 * Covered allocation points (counts probed at runtime with stdlib_alloc_seq()):
 *   - intset::stdlib_rebuild ................ 2 buffers (keys, state)
 *       * first insert into an empty set (empty -> cap 8)
 *       * growth (5 live entries, cap 8 -> 16)
 *   - deque::reserve / stdlib_make_room ..... 1 buffer (ring copy)
 *       * first push_back  (empty -> cap 8)
 *       * first push_front (empty -> cap 8)
 *       * growth 8 -> 16 with a WRAPPED ring (head != 0)
 *   - list::push_back / push_front .......... 1 node each
 *       * into an empty list and into a non-empty list
 *   - stack::push growth .................... 1 buffer
 *       * first push (empty -> cap 8) and growth 8 -> 16
 *   - queue::push growth (ring copy) ........ 1 buffer
 *       * first push (empty -> cap 8) and growth 8 -> 16 with a WRAPPED ring
 *
 * METHOD. For each operation:
 *   1. PROBE a fresh, identical container: reset injection, run the operation
 *      successfully, read stdlib_alloc_seq() -> `count`.  Assert count >= 1.
 *   2. For N = 1..count, re-run the SAME operation on a freshly built identical
 *      container A with stdlib_alloc_fail_at(N) and assert:
 *        (a) return value == -1
 *        (b) every pre-existing element is still readable with its value, and
 *            the newly inserted element is absent
 *        (c) size / capacity / valid() are unchanged from the pre-state
 *        (d) telemetry alloc_count == free_count after drop (no leak)
 *        (e) after stdlib_alloc_fail_reset() the same operation succeeds on a
 *            freshly built identical container B
 *   No system memory is ever exhausted: failure is a logical counter in the
 *   checked allocator.  Successful allocations alone are NOT taken as proof of
 *   memory safety -- only atomic rollback + balanced telemetry is.
 *
 * SELF-CHECKING: each check() prints "<tag> <1|0>"; the final line is
 * "ALLOC <checks> <failures>".  Exit 0 == all checks passed, 1 == a failure.
 * Every container variable has a unique name: the C+ lowering resolves a
 * method call by the owning variable's name within a scope, so reusing a name
 * for two different container types would dispatch the wrong method.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

static int g_checks = 0;
static int g_failed = 0;

static void check(const char *label, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(label, cond ? 1 : 0);
}

/* ---- intset: first insert (empty -> cap 8), rebuild allocates 2 buffers --- */
static void test_intset_first(void) {
    int N, rc, rc2, ok, ok2;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;

    stdlib_alloc_fail_reset();
    {
        intset isf_probe = intset();
        rc = isf_probe.insert(777);
        seqc = stdlib_alloc_seq();
        check("isf_probe", rc == 0 && seqc >= 1 && seqc <= 2 &&
                           isf_probe.size() == 1 && isf_probe.valid() == 1);
        isf_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        intset isf_a = intset();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = isf_a.insert(777);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && isf_a.size() == 0 && isf_a.valid() == 0 &&
             isf_a.contains(777) == 0;
        snprintf(lbl, sizeof(lbl), "isf_o%d", N);
        check(lbl, ok);
        isf_a.drop();

        {
            intset isf_b = intset();
            stdlib_alloc_fail_reset();
            rc2 = isf_b.insert(777);
            ok2 = rc2 == 0 && isf_b.size() == 1 && isf_b.valid() == 1 &&
                  isf_b.contains(777) == 1;
            snprintf(lbl, sizeof(lbl), "isf_r%d", N);
            check(lbl, ok2);
            isf_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "isf_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- intset: growth from 5 entries (cap 8 -> 16), 2 rebuild buffers ------ */
static void test_intset_grow(void) {
    int N, i, rc, rc2, ok, ok2;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;
    int isg_pre[5];
    isg_pre[0] = 10; isg_pre[1] = 20; isg_pre[2] = 30; isg_pre[3] = 40; isg_pre[4] = 50;

    stdlib_alloc_fail_reset();
    {
        intset isg_probe = intset();
        for (i = 0; i < 5; i++)
            isg_probe.insert(isg_pre[i]);
        stdlib_alloc_fail_reset();
        rc = isg_probe.insert(777);
        seqc = stdlib_alloc_seq();
        check("isg_probe", rc == 0 && seqc >= 1 && seqc <= 2 &&
                           isg_probe.size() == 6);
        isg_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        intset isg_a = intset();
        for (i = 0; i < 5; i++)
            isg_a.insert(isg_pre[i]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = isg_a.insert(777);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && isg_a.size() == 5 && isg_a.valid() == 1 &&
             isg_a.contains(777) == 0;
        for (i = 0; i < 5; i++)
            if (!isg_a.contains(isg_pre[i]))
                ok = 0;
        snprintf(lbl, sizeof(lbl), "isg_o%d", N);
        check(lbl, ok);
        isg_a.drop();

        {
            intset isg_b = intset();
            for (i = 0; i < 5; i++)
                isg_b.insert(isg_pre[i]);
            stdlib_alloc_fail_reset();
            rc2 = isg_b.insert(777);
            ok2 = rc2 == 0 && isg_b.size() == 6 && isg_b.contains(777) == 1;
            for (i = 0; i < 5; i++)
                if (!isg_b.contains(isg_pre[i]))
                    ok2 = 0;
            snprintf(lbl, sizeof(lbl), "isg_r%d", N);
            check(lbl, ok2);
            isg_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "isg_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- deque: first push_back from empty (1 ring-copy buffer) -------------- */
static void test_deque_push_back_first(void) {
    int N, rc, rc2, ok, ok2, v_dpbf = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;

    stdlib_alloc_fail_reset();
    {
        deque dpbf_probe = deque();
        stdlib_alloc_fail_reset();
        rc = dpbf_probe.push_back(555);
        seqc = stdlib_alloc_seq();
        check("dpbf_probe", rc == 0 && seqc == 1 &&
                            dpbf_probe.size() == 1 && dpbf_probe.capacity() == 8);
        dpbf_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        deque dpbf_a = deque();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = dpbf_a.push_back(555);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && dpbf_a.size() == 0 && dpbf_a.capacity() == 0 &&
             dpbf_a.at_checked(0, &v_dpbf) == -1;
        snprintf(lbl, sizeof(lbl), "dpbf_o%d", N);
        check(lbl, ok);
        dpbf_a.drop();

        {
            deque dpbf_b = deque();
            stdlib_alloc_fail_reset();
            rc2 = dpbf_b.push_back(555);
            ok2 = rc2 == 0 && dpbf_b.size() == 1 && dpbf_b.capacity() == 8 &&
                  dpbf_b.at_checked(0, &v_dpbf) == 0 && v_dpbf == 555;
            snprintf(lbl, sizeof(lbl), "dpbf_r%d", N);
            check(lbl, ok2);
            dpbf_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "dpbf_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- deque: first push_front from empty (1 ring-copy buffer) ------------- */
static void test_deque_push_front_first(void) {
    int N, rc, rc2, ok, ok2, v_dpff = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;

    stdlib_alloc_fail_reset();
    {
        deque dpff_probe = deque();
        stdlib_alloc_fail_reset();
        rc = dpff_probe.push_front(444);
        seqc = stdlib_alloc_seq();
        check("dpff_probe", rc == 0 && seqc == 1 &&
                            dpff_probe.size() == 1 && dpff_probe.capacity() == 8);
        dpff_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        deque dpff_a = deque();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = dpff_a.push_front(444);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && dpff_a.size() == 0 && dpff_a.capacity() == 0 &&
             dpff_a.at_checked(0, &v_dpff) == -1;
        snprintf(lbl, sizeof(lbl), "dpff_o%d", N);
        check(lbl, ok);
        dpff_a.drop();

        {
            deque dpff_b = deque();
            stdlib_alloc_fail_reset();
            rc2 = dpff_b.push_front(444);
            ok2 = rc2 == 0 && dpff_b.size() == 1 && dpff_b.capacity() == 8 &&
                  dpff_b.at_checked(0, &v_dpff) == 0 && v_dpff == 444;
            snprintf(lbl, sizeof(lbl), "dpff_r%d", N);
            check(lbl, ok2);
            dpff_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "dpff_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- deque: growth 8 -> 16 with a wrapped ring (head = 2) ---------------- */
static void test_deque_grow_wrapped(void) {
    int N, i, rc, rc2, ok, ok2, v_dgw = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;
    int expect[8];
    expect[0] = 2; expect[1] = 3; expect[2] = 4; expect[3] = 5;
    expect[4] = 6; expect[5] = 7; expect[6] = 100; expect[7] = 101;

    stdlib_alloc_fail_reset();
    {
        deque dgw_probe = deque();
        for (i = 0; i < 8; i++)
            dgw_probe.push_back(i);
        for (i = 0; i < 2; i++)
            dgw_probe.pop_front(&v_dgw);
        for (i = 0; i < 2; i++)
            dgw_probe.push_back(100 + i);
        stdlib_alloc_fail_reset();
        rc = dgw_probe.push_back(555);
        seqc = stdlib_alloc_seq();
        check("dgw_probe", rc == 0 && seqc == 1 && dgw_probe.size() == 9 &&
                           dgw_probe.capacity() == 16);
        dgw_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        deque dgw_a = deque();
        for (i = 0; i < 8; i++)
            dgw_a.push_back(i);
        for (i = 0; i < 2; i++)
            dgw_a.pop_front(&v_dgw);
        for (i = 0; i < 2; i++)
            dgw_a.push_back(100 + i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = dgw_a.push_back(555);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && dgw_a.size() == 8 && dgw_a.capacity() == 8;
        for (i = 0; i < 8; i++)
            if (dgw_a.at_checked((size_t)i, &v_dgw) != 0 || v_dgw != expect[i])
                ok = 0;
        snprintf(lbl, sizeof(lbl), "dgw_o%d", N);
        check(lbl, ok);
        dgw_a.drop();

        {
            deque dgw_b = deque();
            for (i = 0; i < 8; i++)
                dgw_b.push_back(i);
            for (i = 0; i < 2; i++)
                dgw_b.pop_front(&v_dgw);
            for (i = 0; i < 2; i++)
                dgw_b.push_back(100 + i);
            stdlib_alloc_fail_reset();
            rc2 = dgw_b.push_back(555);
            ok2 = rc2 == 0 && dgw_b.size() == 9 && dgw_b.capacity() == 16;
            for (i = 0; i < 8; i++)
                if (dgw_b.at_checked((size_t)i, &v_dgw) != 0 || v_dgw != expect[i])
                    ok2 = 0;
            if (dgw_b.at_checked(8, &v_dgw) != 0 || v_dgw != 555)
                ok2 = 0;
            snprintf(lbl, sizeof(lbl), "dgw_r%d", N);
            check(lbl, ok2);
            dgw_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "dgw_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- list: push_back into an empty list (1 node) ------------------------- */
static void test_list_push_back_empty(void) {
    int N, rc, rc2, ok, ok2, v_lpbe = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;

    stdlib_alloc_fail_reset();
    {
        list lpbe_probe = list();
        stdlib_alloc_fail_reset();
        rc = lpbe_probe.push_back(321);
        seqc = stdlib_alloc_seq();
        check("lpbe_probe", rc == 0 && seqc == 1 && lpbe_probe.size() == 1);
        lpbe_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        list lpbe_a = list();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = lpbe_a.push_back(321);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && lpbe_a.size() == 0;
        snprintf(lbl, sizeof(lbl), "lpbe_o%d", N);
        check(lbl, ok);
        lpbe_a.drop();

        {
            list lpbe_b = list();
            stdlib_alloc_fail_reset();
            rc2 = lpbe_b.push_back(321);
            ok2 = rc2 == 0 && lpbe_b.size() == 1 &&
                  lpbe_b.at_checked(0, &v_lpbe) == 0 && v_lpbe == 321;
            snprintf(lbl, sizeof(lbl), "lpbe_r%d", N);
            check(lbl, ok2);
            lpbe_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "lpbe_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- list: push_back into a non-empty list (1 node) ---------------------- */
static void test_list_push_back_nonempty(void) {
    int N, i, rc, rc2, ok, ok2, v_lpbn = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;
    int lpbn_pre[3];
    lpbn_pre[0] = 11; lpbn_pre[1] = 22; lpbn_pre[2] = 33;

    stdlib_alloc_fail_reset();
    {
        list lpbn_probe = list();
        for (i = 0; i < 3; i++)
            lpbn_probe.push_back(lpbn_pre[i]);
        stdlib_alloc_fail_reset();
        rc = lpbn_probe.push_back(321);
        seqc = stdlib_alloc_seq();
        check("lpbn_probe", rc == 0 && seqc == 1 && lpbn_probe.size() == 4);
        lpbn_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        list lpbn_a = list();
        for (i = 0; i < 3; i++)
            lpbn_a.push_back(lpbn_pre[i]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = lpbn_a.push_back(321);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && lpbn_a.size() == 3;
        for (i = 0; i < 3; i++)
            if (lpbn_a.at_checked((size_t)i, &v_lpbn) != 0 || v_lpbn != lpbn_pre[i])
                ok = 0;
        snprintf(lbl, sizeof(lbl), "lpbn_o%d", N);
        check(lbl, ok);
        lpbn_a.drop();

        {
            list lpbn_b = list();
            for (i = 0; i < 3; i++)
                lpbn_b.push_back(lpbn_pre[i]);
            stdlib_alloc_fail_reset();
            rc2 = lpbn_b.push_back(321);
            ok2 = rc2 == 0 && lpbn_b.size() == 4;
            for (i = 0; i < 3; i++)
                if (lpbn_b.at_checked((size_t)i, &v_lpbn) != 0 || v_lpbn != lpbn_pre[i])
                    ok2 = 0;
            if (lpbn_b.at_checked(3, &v_lpbn) != 0 || v_lpbn != 321)
                ok2 = 0;
            snprintf(lbl, sizeof(lbl), "lpbn_r%d", N);
            check(lbl, ok2);
            lpbn_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "lpbn_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- list: push_front into an empty list (1 node) ------------------------ */
static void test_list_push_front_empty(void) {
    int N, rc, rc2, ok, ok2, v_lpfe = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;

    stdlib_alloc_fail_reset();
    {
        list lpfe_probe = list();
        stdlib_alloc_fail_reset();
        rc = lpfe_probe.push_front(123);
        seqc = stdlib_alloc_seq();
        check("lpfe_probe", rc == 0 && seqc == 1 && lpfe_probe.size() == 1);
        lpfe_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        list lpfe_a = list();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = lpfe_a.push_front(123);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && lpfe_a.size() == 0;
        snprintf(lbl, sizeof(lbl), "lpfe_o%d", N);
        check(lbl, ok);
        lpfe_a.drop();

        {
            list lpfe_b = list();
            stdlib_alloc_fail_reset();
            rc2 = lpfe_b.push_front(123);
            ok2 = rc2 == 0 && lpfe_b.size() == 1 &&
                  lpfe_b.at_checked(0, &v_lpfe) == 0 && v_lpfe == 123;
            snprintf(lbl, sizeof(lbl), "lpfe_r%d", N);
            check(lbl, ok2);
            lpfe_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "lpfe_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- list: push_front into a non-empty list (1 node) --------------------- */
static void test_list_push_front_nonempty(void) {
    int N, i, rc, rc2, ok, ok2, v_lpfn = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;
    int lpfn_pre[3];
    lpfn_pre[0] = 11; lpfn_pre[1] = 22; lpfn_pre[2] = 33;

    stdlib_alloc_fail_reset();
    {
        list lpfn_probe = list();
        for (i = 0; i < 3; i++)
            lpfn_probe.push_back(lpfn_pre[i]);
        stdlib_alloc_fail_reset();
        rc = lpfn_probe.push_front(123);
        seqc = stdlib_alloc_seq();
        check("lpfn_probe", rc == 0 && seqc == 1 && lpfn_probe.size() == 4);
        lpfn_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        list lpfn_a = list();
        for (i = 0; i < 3; i++)
            lpfn_a.push_back(lpfn_pre[i]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = lpfn_a.push_front(123);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && lpfn_a.size() == 3;
        for (i = 0; i < 3; i++)
            if (lpfn_a.at_checked((size_t)i, &v_lpfn) != 0 || v_lpfn != lpfn_pre[i])
                ok = 0;
        snprintf(lbl, sizeof(lbl), "lpfn_o%d", N);
        check(lbl, ok);
        lpfn_a.drop();

        {
            list lpfn_b = list();
            for (i = 0; i < 3; i++)
                lpfn_b.push_back(lpfn_pre[i]);
            stdlib_alloc_fail_reset();
            rc2 = lpfn_b.push_front(123);
            ok2 = rc2 == 0 && lpfn_b.size() == 4;
            if (lpfn_b.at_checked(0, &v_lpfn) != 0 || v_lpfn != 123)
                ok2 = 0;
            for (i = 0; i < 3; i++)
                if (lpfn_b.at_checked((size_t)(i + 1), &v_lpfn) != 0 ||
                    v_lpfn != lpfn_pre[i])
                    ok2 = 0;
            snprintf(lbl, sizeof(lbl), "lpfn_r%d", N);
            check(lbl, ok2);
            lpfn_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "lpfn_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- stack: first push (1 buffer) ---------------------------------------- */
static void test_stack_first(void) {
    int N, rc, rc2, ok, ok2, v_stf = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;

    stdlib_alloc_fail_reset();
    {
        stack stf_probe = stack();
        stdlib_alloc_fail_reset();
        rc = stf_probe.push(654);
        seqc = stdlib_alloc_seq();
        check("stf_probe", rc == 0 && seqc == 1 && stf_probe.size() == 1 &&
                           stf_probe.top_checked(&v_stf) == 0 && v_stf == 654);
        stf_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        stack stf_a = stack();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = stf_a.push(654);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && stf_a.size() == 0 &&
             stf_a.top_checked(&v_stf) == -1;
        snprintf(lbl, sizeof(lbl), "stf_o%d", N);
        check(lbl, ok);
        stf_a.drop();

        {
            stack stf_b = stack();
            stdlib_alloc_fail_reset();
            rc2 = stf_b.push(654);
            ok2 = rc2 == 0 && stf_b.size() == 1 &&
                  stf_b.top_checked(&v_stf) == 0 && v_stf == 654;
            snprintf(lbl, sizeof(lbl), "stf_r%d", N);
            check(lbl, ok2);
            stf_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "stf_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- stack: growth 8 -> 16 (1 buffer) ------------------------------------ */
static void test_stack_grow(void) {
    int N, i, rc, rc2, ok, ok2, v_stg = 0, want;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;

    stdlib_alloc_fail_reset();
    {
        stack stg_probe = stack();
        for (i = 0; i < 8; i++)
            stg_probe.push(i);
        stdlib_alloc_fail_reset();
        rc = stg_probe.push(654);
        seqc = stdlib_alloc_seq();
        check("stg_probe", rc == 0 && seqc == 1 && stg_probe.size() == 9 &&
                           stg_probe.top_checked(&v_stg) == 0 && v_stg == 654);
        stg_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        stack stg_a = stack();
        for (i = 0; i < 8; i++)
            stg_a.push(i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = stg_a.push(654);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && stg_a.size() == 8 &&
             stg_a.top_checked(&v_stg) == 0 && v_stg == 7;
        for (i = 0; i < 8; i++) {
            want = 7 - i;
            if (stg_a.pop(&v_stg) != 0 || v_stg != want)
                ok = 0;
        }
        if (stg_a.size() != 0)
            ok = 0;
        snprintf(lbl, sizeof(lbl), "stg_o%d", N);
        check(lbl, ok);
        stg_a.drop();

        {
            stack stg_b = stack();
            for (i = 0; i < 8; i++)
                stg_b.push(i);
            stdlib_alloc_fail_reset();
            rc2 = stg_b.push(654);
            ok2 = rc2 == 0 && stg_b.size() == 9 &&
                  stg_b.top_checked(&v_stg) == 0 && v_stg == 654;
            /* drain the 9 values: 654, then 7..0 */
            if (stg_b.pop(&v_stg) != 0 || v_stg != 654)
                ok2 = 0;
            for (i = 0; i < 8; i++) {
                want = 7 - i;
                if (stg_b.pop(&v_stg) != 0 || v_stg != want)
                    ok2 = 0;
            }
            snprintf(lbl, sizeof(lbl), "stg_r%d", N);
            check(lbl, ok2);
            stg_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "stg_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- queue: first push (1 ring-copy buffer) ------------------------------ */
static void test_queue_first(void) {
    int N, rc, rc2, ok, ok2, v_qf = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;

    stdlib_alloc_fail_reset();
    {
        queue qf_probe = queue();
        stdlib_alloc_fail_reset();
        rc = qf_probe.push(987);
        seqc = stdlib_alloc_seq();
        check("qf_probe", rc == 0 && seqc == 1 && qf_probe.size() == 1 &&
                          qf_probe.front_checked(&v_qf) == 0 && v_qf == 987);
        qf_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        queue qf_a = queue();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = qf_a.push(987);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && qf_a.size() == 0 &&
             qf_a.front_checked(&v_qf) == -1;
        snprintf(lbl, sizeof(lbl), "qf_o%d", N);
        check(lbl, ok);
        qf_a.drop();

        {
            queue qf_b = queue();
            stdlib_alloc_fail_reset();
            rc2 = qf_b.push(987);
            ok2 = rc2 == 0 && qf_b.size() == 1 &&
                  qf_b.front_checked(&v_qf) == 0 && v_qf == 987;
            snprintf(lbl, sizeof(lbl), "qf_r%d", N);
            check(lbl, ok2);
            qf_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "qf_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

/* ---- queue: growth 8 -> 16 with a wrapped ring (1 ring-copy buffer) ------ */
static void test_queue_grow_wrapped(void) {
    int N, i, rc, rc2, ok, ok2, v_qgw = 0, want;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;
    int expect[8];
    expect[0] = 2; expect[1] = 3; expect[2] = 4; expect[3] = 5;
    expect[4] = 6; expect[5] = 7; expect[6] = 100; expect[7] = 101;

    stdlib_alloc_fail_reset();
    {
        queue qgw_probe = queue();
        for (i = 0; i < 8; i++)
            qgw_probe.push(i);
        for (i = 0; i < 2; i++)
            qgw_probe.pop(&v_qgw);
        for (i = 0; i < 2; i++)
            qgw_probe.push(100 + i);
        stdlib_alloc_fail_reset();
        rc = qgw_probe.push(987);
        seqc = stdlib_alloc_seq();
        check("qgw_probe", rc == 0 && seqc == 1 && qgw_probe.size() == 9);
        qgw_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        queue qgw_a = queue();
        for (i = 0; i < 8; i++)
            qgw_a.push(i);
        for (i = 0; i < 2; i++)
            qgw_a.pop(&v_qgw);
        for (i = 0; i < 2; i++)
            qgw_a.push(100 + i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = qgw_a.push(987);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && qgw_a.size() == 8;
        for (i = 0; i < 8; i++) {
            want = expect[i];
            if (qgw_a.pop(&v_qgw) != 0 || v_qgw != want)
                ok = 0;
        }
        if (qgw_a.size() != 0)
            ok = 0;
        snprintf(lbl, sizeof(lbl), "qgw_o%d", N);
        check(lbl, ok);
        qgw_a.drop();

        {
            queue qgw_b = queue();
            for (i = 0; i < 8; i++)
                qgw_b.push(i);
            for (i = 0; i < 2; i++)
                qgw_b.pop(&v_qgw);
            for (i = 0; i < 2; i++)
                qgw_b.push(100 + i);
            stdlib_alloc_fail_reset();
            rc2 = qgw_b.push(987);
            ok2 = rc2 == 0 && qgw_b.size() == 9;
            for (i = 0; i < 8; i++) {
                want = expect[i];
                if (qgw_b.pop(&v_qgw) != 0 || v_qgw != want)
                    ok2 = 0;
            }
            if (qgw_b.pop(&v_qgw) != 0 || v_qgw != 987)
                ok2 = 0;
            snprintf(lbl, sizeof(lbl), "qgw_r%d", N);
            check(lbl, ok2);
            qgw_b.drop();
        }
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "qgw_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    }
}

int main(void) {
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);

    test_intset_first();
    test_intset_grow();
    test_deque_push_back_first();
    test_deque_push_front_first();
    test_deque_grow_wrapped();
    test_list_push_back_empty();
    test_list_push_back_nonempty();
    test_list_push_front_empty();
    test_list_push_front_nonempty();
    test_stack_first();
    test_stack_grow();
    test_queue_first();
    test_queue_grow_wrapped();

    check("alloc_no_fatal", 1);
    stdlib_telemetry_set_enabled(0);

    print("ALLOC", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
