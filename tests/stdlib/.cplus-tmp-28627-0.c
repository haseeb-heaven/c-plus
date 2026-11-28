#if defined(CPLUS_AUTO_RUNTIME)
extern void cplus_set_auto_runtime(int);
static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
  cplus_set_auto_runtime(1);
}
#endif
#include <stddef.h>
extern void cplus_zero(void *, size_t);
#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "tests/stdlib/deep_collections_alloc.cp"
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
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-28627-0.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-28627-1.h"

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
        intset isf_probe = intset_ctor();
        rc = intset_insert(&isf_probe, 777);
        seqc = stdlib_alloc_seq();
        check("isf_probe", rc == 0 && seqc >= 1 && seqc <= 2 &&
                           intset_size(&isf_probe) == 1 && intset_valid(&isf_probe) == 1);
        { intset_drop ( & isf_probe ); (void)cplus_zero(&isf_probe, sizeof(isf_probe)); }
    intset_drop(&isf_probe); 
#line 73 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        intset isf_a = intset_ctor();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = intset_insert(&isf_a, 777);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && intset_size(&isf_a) == 0 && intset_valid(&isf_a) == 0 &&
             intset_contains(&isf_a, 777) == 0;
        snprintf(lbl, sizeof(lbl), "isf_o%d", N);
        check(lbl, ok);
        { intset_drop ( & isf_a ); (void)cplus_zero(&isf_a, sizeof(isf_a)); }

        {
            intset isf_b = intset_ctor();
            stdlib_alloc_fail_reset();
            rc2 = intset_insert(&isf_b, 777);
            ok2 = rc2 == 0 && intset_size(&isf_b) == 1 && intset_valid(&isf_b) == 1 &&
                  intset_contains(&isf_b, 777) == 1;
            snprintf(lbl, sizeof(lbl), "isf_r%d", N);
            check(lbl, ok2);
            { intset_drop ( & isf_b ); (void)cplus_zero(&isf_b, sizeof(isf_b)); }
        intset_drop(&isf_b); 
#line 97 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "isf_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    intset_drop(&isf_a); 
#line 102 "tests/stdlib/deep_collections_alloc.cp"
}
}

/* ---- intset: growth from 5 entries (cap 8 -> 16), 2 rebuild buffers ------ */
static void test_intset_grow(void) {
    int N, i, rc, rc2, ok, ok2;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;
    int pre[5];
    pre[0] = 10; pre[1] = 20; pre[2] = 30; pre[3] = 40; pre[4] = 50;

    stdlib_alloc_fail_reset();
    {
        intset isg_probe = intset_ctor();
        for (i = 0; i < 5; i++)
            intset_insert(&isg_probe, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
        stdlib_alloc_fail_reset();
        rc = intset_insert(&isg_probe, 777);
        seqc = stdlib_alloc_seq();
        check("isg_probe", rc == 0 && seqc >= 1 && seqc <= 2 &&
                           intset_size(&isg_probe) == 6);
        { intset_drop ( & isg_probe ); (void)cplus_zero(&isg_probe, sizeof(isg_probe)); }
    intset_drop(&isg_probe); 
#line 125 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        intset isg_a = intset_ctor();
        for (i = 0; i < 5; i++)
            intset_insert(&isg_a, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = intset_insert(&isg_a, 777);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && intset_size(&isg_a) == 5 && intset_valid(&isg_a) == 1 &&
             intset_contains(&isg_a, 777) == 0;
        for (i = 0; i < 5; i++)
            if (!intset_contains(&isg_a, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]))
                ok = 0;
        snprintf(lbl, sizeof(lbl), "isg_o%d", N);
        check(lbl, ok);
        { intset_drop ( & isg_a ); (void)cplus_zero(&isg_a, sizeof(isg_a)); }

        {
            intset isg_b = intset_ctor();
            for (i = 0; i < 5; i++)
                intset_insert(&isg_b, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
            stdlib_alloc_fail_reset();
            rc2 = intset_insert(&isg_b, 777);
            ok2 = rc2 == 0 && intset_size(&isg_b) == 6 && intset_contains(&isg_b, 777) == 1;
            for (i = 0; i < 5; i++)
                if (!intset_contains(&isg_b, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]))
                    ok2 = 0;
            snprintf(lbl, sizeof(lbl), "isg_r%d", N);
            check(lbl, ok2);
            { intset_drop ( & isg_b ); (void)cplus_zero(&isg_b, sizeof(isg_b)); }
        intset_drop(&isg_b); 
#line 158 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "isg_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    intset_drop(&isg_a); 
#line 163 "tests/stdlib/deep_collections_alloc.cp"
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
        deque dpbf_probe = deque_ctor();
        stdlib_alloc_fail_reset();
        rc = deque_push_back(&dpbf_probe, 555);
        seqc = stdlib_alloc_seq();
        check("dpbf_probe", rc == 0 && seqc == 1 &&
                            deque_size(&dpbf_probe) == 1 && deque_capacity(&dpbf_probe) == 8);
        { deque_drop ( & dpbf_probe ); (void)cplus_zero(&dpbf_probe, sizeof(dpbf_probe)); }
    deque_drop(&dpbf_probe); 
#line 182 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        deque dpbf_a = deque_ctor();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = deque_push_back(&dpbf_a, 555);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && deque_size(&dpbf_a) == 0 && deque_capacity(&dpbf_a) == 0 &&
             deque_at_checked(&dpbf_a, 0, &v_dpbf) == -1;
        snprintf(lbl, sizeof(lbl), "dpbf_o%d", N);
        check(lbl, ok);
        { deque_drop ( & dpbf_a ); (void)cplus_zero(&dpbf_a, sizeof(dpbf_a)); }

        {
            deque dpbf_b = deque_ctor();
            stdlib_alloc_fail_reset();
            rc2 = deque_push_back(&dpbf_b, 555);
            ok2 = rc2 == 0 && deque_size(&dpbf_b) == 1 && deque_capacity(&dpbf_b) == 8 &&
                  deque_at_checked(&dpbf_b, 0, &v_dpbf) == 0 && v_dpbf == 555;
            snprintf(lbl, sizeof(lbl), "dpbf_r%d", N);
            check(lbl, ok2);
            { deque_drop ( & dpbf_b ); (void)cplus_zero(&dpbf_b, sizeof(dpbf_b)); }
        deque_drop(&dpbf_b); 
#line 206 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "dpbf_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    deque_drop(&dpbf_a); 
#line 211 "tests/stdlib/deep_collections_alloc.cp"
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
        deque dpff_probe = deque_ctor();
        stdlib_alloc_fail_reset();
        rc = deque_push_front(&dpff_probe, 444);
        seqc = stdlib_alloc_seq();
        check("dpff_probe", rc == 0 && seqc == 1 &&
                            deque_size(&dpff_probe) == 1 && deque_capacity(&dpff_probe) == 8);
        { deque_drop ( & dpff_probe ); (void)cplus_zero(&dpff_probe, sizeof(dpff_probe)); }
    deque_drop(&dpff_probe); 
#line 230 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        deque dpff_a = deque_ctor();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = deque_push_front(&dpff_a, 444);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && deque_size(&dpff_a) == 0 && deque_capacity(&dpff_a) == 0 &&
             deque_at_checked(&dpff_a, 0, &v_dpff) == -1;
        snprintf(lbl, sizeof(lbl), "dpff_o%d", N);
        check(lbl, ok);
        { deque_drop ( & dpff_a ); (void)cplus_zero(&dpff_a, sizeof(dpff_a)); }

        {
            deque dpff_b = deque_ctor();
            stdlib_alloc_fail_reset();
            rc2 = deque_push_front(&dpff_b, 444);
            ok2 = rc2 == 0 && deque_size(&dpff_b) == 1 && deque_capacity(&dpff_b) == 8 &&
                  deque_at_checked(&dpff_b, 0, &v_dpff) == 0 && v_dpff == 444;
            snprintf(lbl, sizeof(lbl), "dpff_r%d", N);
            check(lbl, ok2);
            { deque_drop ( & dpff_b ); (void)cplus_zero(&dpff_b, sizeof(dpff_b)); }
        deque_drop(&dpff_b); 
#line 254 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "dpff_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    deque_drop(&dpff_a); 
#line 259 "tests/stdlib/deep_collections_alloc.cp"
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
        deque dgw_probe = deque_ctor();
        for (i = 0; i < 8; i++)
            deque_push_back(&dgw_probe, i);
        for (i = 0; i < 2; i++)
            deque_pop_front(&dgw_probe, &v_dgw);
        for (i = 0; i < 2; i++)
            deque_push_back(&dgw_probe, 100 + i);
        stdlib_alloc_fail_reset();
        rc = deque_push_back(&dgw_probe, 555);
        seqc = stdlib_alloc_seq();
        check("dgw_probe", rc == 0 && seqc == 1 && deque_size(&dgw_probe) == 9 &&
                           deque_capacity(&dgw_probe) == 16);
        { deque_drop ( & dgw_probe ); (void)cplus_zero(&dgw_probe, sizeof(dgw_probe)); }
    deque_drop(&dgw_probe); 
#line 287 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        deque dgw_a = deque_ctor();
        for (i = 0; i < 8; i++)
            deque_push_back(&dgw_a, i);
        for (i = 0; i < 2; i++)
            deque_pop_front(&dgw_a, &v_dgw);
        for (i = 0; i < 2; i++)
            deque_push_back(&dgw_a, 100 + i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = deque_push_back(&dgw_a, 555);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && deque_size(&dgw_a) == 8 && deque_capacity(&dgw_a) == 8;
        for (i = 0; i < 8; i++)
            if (deque_at_checked(&dgw_a, (size_t)i, &v_dgw) != 0 || v_dgw != expect[__cplus_bounds_check((size_t)(i), 8, "expect")])
                ok = 0;
        snprintf(lbl, sizeof(lbl), "dgw_o%d", N);
        check(lbl, ok);
        { deque_drop ( & dgw_a ); (void)cplus_zero(&dgw_a, sizeof(dgw_a)); }

        {
            deque dgw_b = deque_ctor();
            for (i = 0; i < 8; i++)
                deque_push_back(&dgw_b, i);
            for (i = 0; i < 2; i++)
                deque_pop_front(&dgw_b, &v_dgw);
            for (i = 0; i < 2; i++)
                deque_push_back(&dgw_b, 100 + i);
            stdlib_alloc_fail_reset();
            rc2 = deque_push_back(&dgw_b, 555);
            ok2 = rc2 == 0 && deque_size(&dgw_b) == 9 && deque_capacity(&dgw_b) == 16;
            for (i = 0; i < 8; i++)
                if (deque_at_checked(&dgw_b, (size_t)i, &v_dgw) != 0 || v_dgw != expect[__cplus_bounds_check((size_t)(i), 8, "expect")])
                    ok2 = 0;
            if (deque_at_checked(&dgw_b, 8, &v_dgw) != 0 || v_dgw != 555)
                ok2 = 0;
            snprintf(lbl, sizeof(lbl), "dgw_r%d", N);
            check(lbl, ok2);
            { deque_drop ( & dgw_b ); (void)cplus_zero(&dgw_b, sizeof(dgw_b)); }
        deque_drop(&dgw_b); 
#line 329 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "dgw_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    deque_drop(&dgw_a); 
#line 334 "tests/stdlib/deep_collections_alloc.cp"
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
        list lpbe_probe = list_ctor();
        stdlib_alloc_fail_reset();
        rc = list_push_back(&lpbe_probe, 321);
        seqc = stdlib_alloc_seq();
        check("lpbe_probe", rc == 0 && seqc == 1 && list_size(&lpbe_probe) == 1);
        { list_drop ( & lpbe_probe ); (void)cplus_zero(&lpbe_probe, sizeof(lpbe_probe)); }
    list_drop(&lpbe_probe); 
#line 352 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        list lpbe_a = list_ctor();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = list_push_back(&lpbe_a, 321);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && list_size(&lpbe_a) == 0;
        snprintf(lbl, sizeof(lbl), "lpbe_o%d", N);
        check(lbl, ok);
        { list_drop ( & lpbe_a ); (void)cplus_zero(&lpbe_a, sizeof(lpbe_a)); }

        {
            list lpbe_b = list_ctor();
            stdlib_alloc_fail_reset();
            rc2 = list_push_back(&lpbe_b, 321);
            ok2 = rc2 == 0 && list_size(&lpbe_b) == 1 &&
                  list_at_checked(&lpbe_b, 0, &v_lpbe) == 0 && v_lpbe == 321;
            snprintf(lbl, sizeof(lbl), "lpbe_r%d", N);
            check(lbl, ok2);
            { list_drop ( & lpbe_b ); (void)cplus_zero(&lpbe_b, sizeof(lpbe_b)); }
        list_drop(&lpbe_b); 
#line 375 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "lpbe_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    list_drop(&lpbe_a); 
#line 380 "tests/stdlib/deep_collections_alloc.cp"
}
}

/* ---- list: push_back into a non-empty list (1 node) ---------------------- */
static void test_list_push_back_nonempty(void) {
    int N, i, rc, rc2, ok, ok2, v_lpbn = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;
    int pre[3];
    pre[0] = 11; pre[1] = 22; pre[2] = 33;

    stdlib_alloc_fail_reset();
    {
        list lpbn_probe = list_ctor();
        for (i = 0; i < 3; i++)
            list_push_back(&lpbn_probe, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
        stdlib_alloc_fail_reset();
        rc = list_push_back(&lpbn_probe, 321);
        seqc = stdlib_alloc_seq();
        check("lpbn_probe", rc == 0 && seqc == 1 && list_size(&lpbn_probe) == 4);
        { list_drop ( & lpbn_probe ); (void)cplus_zero(&lpbn_probe, sizeof(lpbn_probe)); }
    list_drop(&lpbn_probe); 
#line 402 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        list lpbn_a = list_ctor();
        for (i = 0; i < 3; i++)
            list_push_back(&lpbn_a, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = list_push_back(&lpbn_a, 321);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && list_size(&lpbn_a) == 3;
        for (i = 0; i < 3; i++)
            if (list_at_checked(&lpbn_a, (size_t)i, &v_lpbn) != 0 || v_lpbn != pre[__cplus_bounds_check((size_t)(i), 3, "pre")])
                ok = 0;
        snprintf(lbl, sizeof(lbl), "lpbn_o%d", N);
        check(lbl, ok);
        { list_drop ( & lpbn_a ); (void)cplus_zero(&lpbn_a, sizeof(lpbn_a)); }

        {
            list lpbn_b = list_ctor();
            for (i = 0; i < 3; i++)
                list_push_back(&lpbn_b, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
            stdlib_alloc_fail_reset();
            rc2 = list_push_back(&lpbn_b, 321);
            ok2 = rc2 == 0 && list_size(&lpbn_b) == 4;
            for (i = 0; i < 3; i++)
                if (list_at_checked(&lpbn_b, (size_t)i, &v_lpbn) != 0 || v_lpbn != pre[__cplus_bounds_check((size_t)(i), 3, "pre")])
                    ok2 = 0;
            if (list_at_checked(&lpbn_b, 3, &v_lpbn) != 0 || v_lpbn != 321)
                ok2 = 0;
            snprintf(lbl, sizeof(lbl), "lpbn_r%d", N);
            check(lbl, ok2);
            { list_drop ( & lpbn_b ); (void)cplus_zero(&lpbn_b, sizeof(lpbn_b)); }
        list_drop(&lpbn_b); 
#line 436 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "lpbn_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    list_drop(&lpbn_a); 
#line 441 "tests/stdlib/deep_collections_alloc.cp"
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
        list lpfe_probe = list_ctor();
        stdlib_alloc_fail_reset();
        rc = list_push_front(&lpfe_probe, 123);
        seqc = stdlib_alloc_seq();
        check("lpfe_probe", rc == 0 && seqc == 1 && list_size(&lpfe_probe) == 1);
        { list_drop ( & lpfe_probe ); (void)cplus_zero(&lpfe_probe, sizeof(lpfe_probe)); }
    list_drop(&lpfe_probe); 
#line 459 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        list lpfe_a = list_ctor();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = list_push_front(&lpfe_a, 123);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && list_size(&lpfe_a) == 0;
        snprintf(lbl, sizeof(lbl), "lpfe_o%d", N);
        check(lbl, ok);
        { list_drop ( & lpfe_a ); (void)cplus_zero(&lpfe_a, sizeof(lpfe_a)); }

        {
            list lpfe_b = list_ctor();
            stdlib_alloc_fail_reset();
            rc2 = list_push_front(&lpfe_b, 123);
            ok2 = rc2 == 0 && list_size(&lpfe_b) == 1 &&
                  list_at_checked(&lpfe_b, 0, &v_lpfe) == 0 && v_lpfe == 123;
            snprintf(lbl, sizeof(lbl), "lpfe_r%d", N);
            check(lbl, ok2);
            { list_drop ( & lpfe_b ); (void)cplus_zero(&lpfe_b, sizeof(lpfe_b)); }
        list_drop(&lpfe_b); 
#line 482 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "lpfe_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    list_drop(&lpfe_a); 
#line 487 "tests/stdlib/deep_collections_alloc.cp"
}
}

/* ---- list: push_front into a non-empty list (1 node) --------------------- */
static void test_list_push_front_nonempty(void) {
    int N, i, rc, rc2, ok, ok2, v_lpfn = 0;
    long seqc;
    char lbl[32];
    stdlib_telemetry_stats tmstat;
    int pre[3];
    pre[0] = 11; pre[1] = 22; pre[2] = 33;

    stdlib_alloc_fail_reset();
    {
        list lpfn_probe = list_ctor();
        for (i = 0; i < 3; i++)
            list_push_back(&lpfn_probe, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
        stdlib_alloc_fail_reset();
        rc = list_push_front(&lpfn_probe, 123);
        seqc = stdlib_alloc_seq();
        check("lpfn_probe", rc == 0 && seqc == 1 && list_size(&lpfn_probe) == 4);
        { list_drop ( & lpfn_probe ); (void)cplus_zero(&lpfn_probe, sizeof(lpfn_probe)); }
    list_drop(&lpfn_probe); 
#line 509 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        list lpfn_a = list_ctor();
        for (i = 0; i < 3; i++)
            list_push_back(&lpfn_a, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = list_push_front(&lpfn_a, 123);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && list_size(&lpfn_a) == 3;
        for (i = 0; i < 3; i++)
            if (list_at_checked(&lpfn_a, (size_t)i, &v_lpfn) != 0 || v_lpfn != pre[__cplus_bounds_check((size_t)(i), 3, "pre")])
                ok = 0;
        snprintf(lbl, sizeof(lbl), "lpfn_o%d", N);
        check(lbl, ok);
        { list_drop ( & lpfn_a ); (void)cplus_zero(&lpfn_a, sizeof(lpfn_a)); }

        {
            list lpfn_b = list_ctor();
            for (i = 0; i < 3; i++)
                list_push_back(&lpfn_b, pre[__cplus_bounds_check((size_t)(i), 3, "pre")]);
            stdlib_alloc_fail_reset();
            rc2 = list_push_front(&lpfn_b, 123);
            ok2 = rc2 == 0 && list_size(&lpfn_b) == 4;
            if (list_at_checked(&lpfn_b, 0, &v_lpfn) != 0 || v_lpfn != 123)
                ok2 = 0;
            for (i = 0; i < 3; i++)
                if (list_at_checked(&lpfn_b, (size_t)(i + 1), &v_lpfn) != 0 ||
                    v_lpfn != pre[__cplus_bounds_check((size_t)(i), 3, "pre")])
                    ok2 = 0;
            snprintf(lbl, sizeof(lbl), "lpfn_r%d", N);
            check(lbl, ok2);
            { list_drop ( & lpfn_b ); (void)cplus_zero(&lpfn_b, sizeof(lpfn_b)); }
        list_drop(&lpfn_b); 
#line 544 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "lpfn_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    list_drop(&lpfn_a); 
#line 549 "tests/stdlib/deep_collections_alloc.cp"
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
        stack stf_probe = stack_ctor();
        stdlib_alloc_fail_reset();
        rc = stack_push(&stf_probe, 654);
        seqc = stdlib_alloc_seq();
        check("stf_probe", rc == 0 && seqc == 1 && stack_size(&stf_probe) == 1 &&
                           stack_top_checked(&stf_probe, &v_stf) == 0 && v_stf == 654);
        { stack_drop ( & stf_probe ); (void)cplus_zero(&stf_probe, sizeof(stf_probe)); }
    stack_drop(&stf_probe); 
#line 568 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        stack stf_a = stack_ctor();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = stack_push(&stf_a, 654);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && stack_size(&stf_a) == 0 &&
             stack_top_checked(&stf_a, &v_stf) == -1;
        snprintf(lbl, sizeof(lbl), "stf_o%d", N);
        check(lbl, ok);
        { stack_drop ( & stf_a ); (void)cplus_zero(&stf_a, sizeof(stf_a)); }

        {
            stack stf_b = stack_ctor();
            stdlib_alloc_fail_reset();
            rc2 = stack_push(&stf_b, 654);
            ok2 = rc2 == 0 && stack_size(&stf_b) == 1 &&
                  stack_top_checked(&stf_b, &v_stf) == 0 && v_stf == 654;
            snprintf(lbl, sizeof(lbl), "stf_r%d", N);
            check(lbl, ok2);
            { stack_drop ( & stf_b ); (void)cplus_zero(&stf_b, sizeof(stf_b)); }
        stack_drop(&stf_b); 
#line 592 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "stf_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    stack_drop(&stf_a); 
#line 597 "tests/stdlib/deep_collections_alloc.cp"
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
        stack stg_probe = stack_ctor();
        for (i = 0; i < 8; i++)
            stack_push(&stg_probe, i);
        stdlib_alloc_fail_reset();
        rc = stack_push(&stg_probe, 654);
        seqc = stdlib_alloc_seq();
        check("stg_probe", rc == 0 && seqc == 1 && stack_size(&stg_probe) == 9 &&
                           stack_top_checked(&stg_probe, &v_stg) == 0 && v_stg == 654);
        { stack_drop ( & stg_probe ); (void)cplus_zero(&stg_probe, sizeof(stg_probe)); }
    stack_drop(&stg_probe); 
#line 618 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        stack stg_a = stack_ctor();
        for (i = 0; i < 8; i++)
            stack_push(&stg_a, i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = stack_push(&stg_a, 654);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && stack_size(&stg_a) == 8 &&
             stack_top_checked(&stg_a, &v_stg) == 0 && v_stg == 7;
        for (i = 0; i < 8; i++) {
            want = 7 - i;
            if (stack_pop(&stg_a, &v_stg) != 0 || v_stg != want)
                ok = 0;
        }
        if (stack_size(&stg_a) != 0)
            ok = 0;
        snprintf(lbl, sizeof(lbl), "stg_o%d", N);
        check(lbl, ok);
        { stack_drop ( & stg_a ); (void)cplus_zero(&stg_a, sizeof(stg_a)); }

        {
            stack stg_b = stack_ctor();
            for (i = 0; i < 8; i++)
                stack_push(&stg_b, i);
            stdlib_alloc_fail_reset();
            rc2 = stack_push(&stg_b, 654);
            ok2 = rc2 == 0 && stack_size(&stg_b) == 9 &&
                  stack_top_checked(&stg_b, &v_stg) == 0 && v_stg == 654;
            /* drain the 9 values: 654, then 7..0 */
            if (stack_pop(&stg_b, &v_stg) != 0 || v_stg != 654)
                ok2 = 0;
            for (i = 0; i < 8; i++) {
                want = 7 - i;
                if (stack_pop(&stg_b, &v_stg) != 0 || v_stg != want)
                    ok2 = 0;
            }
            snprintf(lbl, sizeof(lbl), "stg_r%d", N);
            check(lbl, ok2);
            { stack_drop ( & stg_b ); (void)cplus_zero(&stg_b, sizeof(stg_b)); }
        stack_drop(&stg_b); 
#line 661 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "stg_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    stack_drop(&stg_a); 
#line 666 "tests/stdlib/deep_collections_alloc.cp"
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
        queue qf_probe = queue_ctor();
        stdlib_alloc_fail_reset();
        rc = queue_push(&qf_probe, 987);
        seqc = stdlib_alloc_seq();
        check("qf_probe", rc == 0 && seqc == 1 && queue_size(&qf_probe) == 1 &&
                          queue_front_checked(&qf_probe, &v_qf) == 0 && v_qf == 987);
        { queue_drop ( & qf_probe ); (void)cplus_zero(&qf_probe, sizeof(qf_probe)); }
    queue_drop(&qf_probe); 
#line 685 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        queue qf_a = queue_ctor();
        stdlib_telemetry_reset();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = queue_push(&qf_a, 987);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && queue_size(&qf_a) == 0 &&
             queue_front_checked(&qf_a, &v_qf) == -1;
        snprintf(lbl, sizeof(lbl), "qf_o%d", N);
        check(lbl, ok);
        { queue_drop ( & qf_a ); (void)cplus_zero(&qf_a, sizeof(qf_a)); }

        {
            queue qf_b = queue_ctor();
            stdlib_alloc_fail_reset();
            rc2 = queue_push(&qf_b, 987);
            ok2 = rc2 == 0 && queue_size(&qf_b) == 1 &&
                  queue_front_checked(&qf_b, &v_qf) == 0 && v_qf == 987;
            snprintf(lbl, sizeof(lbl), "qf_r%d", N);
            check(lbl, ok2);
            { queue_drop ( & qf_b ); (void)cplus_zero(&qf_b, sizeof(qf_b)); }
        queue_drop(&qf_b); 
#line 709 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "qf_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    queue_drop(&qf_a); 
#line 714 "tests/stdlib/deep_collections_alloc.cp"
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
        queue qgw_probe = queue_ctor();
        for (i = 0; i < 8; i++)
            queue_push(&qgw_probe, i);
        for (i = 0; i < 2; i++)
            queue_pop(&qgw_probe, &v_qgw);
        for (i = 0; i < 2; i++)
            queue_push(&qgw_probe, 100 + i);
        stdlib_alloc_fail_reset();
        rc = queue_push(&qgw_probe, 987);
        seqc = stdlib_alloc_seq();
        check("qgw_probe", rc == 0 && seqc == 1 && queue_size(&qgw_probe) == 9);
        { queue_drop ( & qgw_probe ); (void)cplus_zero(&qgw_probe, sizeof(qgw_probe)); }
    queue_drop(&qgw_probe); 
#line 741 "tests/stdlib/deep_collections_alloc.cp"
}

    for (N = 1; N <= seqc; N++) {
        stdlib_telemetry_reset();
        queue qgw_a = queue_ctor();
        for (i = 0; i < 8; i++)
            queue_push(&qgw_a, i);
        for (i = 0; i < 2; i++)
            queue_pop(&qgw_a, &v_qgw);
        for (i = 0; i < 2; i++)
            queue_push(&qgw_a, 100 + i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = queue_push(&qgw_a, 987);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && queue_size(&qgw_a) == 8;
        for (i = 0; i < 8; i++) {
            want = expect[__cplus_bounds_check((size_t)(i), 8, "expect")];
            if (queue_pop(&qgw_a, &v_qgw) != 0 || v_qgw != want)
                ok = 0;
        }
        if (queue_size(&qgw_a) != 0)
            ok = 0;
        snprintf(lbl, sizeof(lbl), "qgw_o%d", N);
        check(lbl, ok);
        { queue_drop ( & qgw_a ); (void)cplus_zero(&qgw_a, sizeof(qgw_a)); }

        {
            queue qgw_b = queue_ctor();
            for (i = 0; i < 8; i++)
                queue_push(&qgw_b, i);
            for (i = 0; i < 2; i++)
                queue_pop(&qgw_b, &v_qgw);
            for (i = 0; i < 2; i++)
                queue_push(&qgw_b, 100 + i);
            stdlib_alloc_fail_reset();
            rc2 = queue_push(&qgw_b, 987);
            ok2 = rc2 == 0 && queue_size(&qgw_b) == 9;
            for (i = 0; i < 8; i++) {
                want = expect[__cplus_bounds_check((size_t)(i), 8, "expect")];
                if (queue_pop(&qgw_b, &v_qgw) != 0 || v_qgw != want)
                    ok2 = 0;
            }
            if (queue_pop(&qgw_b, &v_qgw) != 0 || v_qgw != 987)
                ok2 = 0;
            snprintf(lbl, sizeof(lbl), "qgw_r%d", N);
            check(lbl, ok2);
            { queue_drop ( & qgw_b ); (void)cplus_zero(&qgw_b, sizeof(qgw_b)); }
        queue_drop(&qgw_b); 
#line 789 "tests/stdlib/deep_collections_alloc.cp"
}
        stdlib_telemetry_snapshot(&tmstat);
        snprintf(lbl, sizeof(lbl), "qgw_lk%d", N);
        check(lbl, tmstat.alloc_count == tmstat.free_count &&
                       tmstat.alloc_failures >= 1);
    queue_drop(&qgw_a); 
#line 794 "tests/stdlib/deep_collections_alloc.cp"
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
