/* Container-safety regression suite -- allocation failure during growth,
 * failure atomicity, destruction after partial init, and double destruction.
 *
 * METHOD. For each fallible growth operation:
 *   1. probe the number of stdlib_alloc calls on a fresh identical container;
 *   2. for N = 1..count, inject stdlib_alloc_fail_at(N) on an identical
 *      container and require rc == -1, every pre-existing element intact with
 *      its value, the new element absent, and size/capacity/valid() unchanged;
 *   3. after reset require the same operation succeeds on a fresh container;
 *   4. require telemetry alloc_count == free_count after drop (no leak) and
 *      alloc_failures >= 1 (the injection was really exercised).
 * No physical memory is exhausted: failure is a logical counter.
 *
 * SELF-CHECKING: "<tag> <1|0>" per check (1 = pass); final line
 * "CONTAINER_SAFETY <checks> <failures>".  All loops are bounded.
 * NOTE: container variable names are unique across functions because C+
 * method lowering resolves a call by the owning variable's name in scope.
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

static void checkn(const char *prefix, int n, int cond) {
    char lbl[40];
    snprintf(lbl, sizeof(lbl), "%s%d", prefix, n);
    check(lbl, cond);
}

/* ---- intmap rebuild: three buffers --------------------------------------- */
static void a_intmap_grow(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;
    int aim_pre[5];
    aim_pre[0] = 10; aim_pre[1] = 20; aim_pre[2] = 30; aim_pre[3] = 40; aim_pre[4] = 50;

    stdlib_alloc_fail_reset();
    {
        intmap aim_probe = intmap();
        for (i = 0; i < 5; i++)
            aim_probe.put(aim_pre[i], i);
        stdlib_alloc_fail_reset();
        rc = aim_probe.put(100, 999);
        seqc = stdlib_alloc_seq();
        check("aim_probe", rc == 0 && seqc >= 1 && seqc <= 3);
        aim_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        intmap aim_a = intmap();
        stdlib_telemetry_reset();
        for (i = 0; i < 5; i++)
            aim_a.put(aim_pre[i], i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = aim_a.put(100, 999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && aim_a.size() == 5 && aim_a.valid() == 1 &&
             aim_a.contains(100) == 0;
        for (i = 0; i < 5; i++)
            if (aim_a.get(aim_pre[i], &o) != 0 || o != i)
                ok = 0;
        checkn("aim_o", N, ok);
        aim_a.drop();

        {
            intmap aim_b = intmap();
            for (i = 0; i < 5; i++)
                aim_b.put(aim_pre[i], i);
            stdlib_alloc_fail_reset();
            rc = aim_b.put(100, 999);
            ok = rc == 0 && aim_b.size() == 6 &&
                 aim_b.get(100, &o) == 0 && o == 999;
            checkn("aim_r", N, ok);
            aim_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        checkn("aim_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

/* ---- strmap rebuild (3 buffers) plus owned-key copy (1) ------------------ */
static void a_strmap_grow(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;
    char asm_keys[5][12];

    stdlib_alloc_fail_reset();
    {
        strmap asm_probe = strmap();
        for (i = 0; i < 5; i++) {
            snprintf(asm_keys[i], sizeof(asm_keys[i]), "p%d", i);
            asm_probe.put(asm_keys[i], i);
        }
        stdlib_alloc_fail_reset();
        rc = asm_probe.put("fresh", 999);
        seqc = stdlib_alloc_seq();
        check("asm_probe", rc == 0 && seqc >= 1 && seqc <= 4);
        asm_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        strmap asm_a = strmap();
        stdlib_telemetry_reset();
        for (i = 0; i < 5; i++) {
            snprintf(asm_keys[i], sizeof(asm_keys[i]), "p%d", i);
            asm_a.put(asm_keys[i], i);
        }
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = asm_a.put("fresh", 999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && asm_a.size() == 5 && asm_a.valid() == 1 &&
             asm_a.contains("fresh") == 0;
        for (i = 0; i < 5; i++) {
            snprintf(asm_keys[i], sizeof(asm_keys[i]), "p%d", i);
            if (asm_a.get(asm_keys[i], &o) != 0 || o != i)
                ok = 0;
        }
        checkn("asm_o", N, ok);
        asm_a.drop();

        {
            strmap asm_b = strmap();
            for (i = 0; i < 5; i++) {
                snprintf(asm_keys[i], sizeof(asm_keys[i]), "p%d", i);
                asm_b.put(asm_keys[i], i);
            }
            stdlib_alloc_fail_reset();
            rc = asm_b.put("fresh", 999);
            ok = rc == 0 && asm_b.size() == 6 &&
                 asm_b.get("fresh", &o) == 0 && o == 999;
            checkn("asm_r", N, ok);
            asm_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        checkn("asm_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

/* ---- intset rebuild: two buffers ----------------------------------------- */
static void a_intset_grow(void) {
    int N, i, rc, ok;
    long seqc;
    stdlib_telemetry_stats tm;
    int ais_pre[5];
    ais_pre[0] = 11; ais_pre[1] = 22; ais_pre[2] = 33; ais_pre[3] = 44; ais_pre[4] = 55;

    stdlib_alloc_fail_reset();
    {
        intset ais_probe = intset();
        for (i = 0; i < 5; i++)
            ais_probe.insert(ais_pre[i]);
        stdlib_alloc_fail_reset();
        rc = ais_probe.insert(777);
        seqc = stdlib_alloc_seq();
        check("ais_probe", rc == 0 && seqc >= 1 && seqc <= 2);
        ais_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        intset ais_a = intset();
        stdlib_telemetry_reset();
        for (i = 0; i < 5; i++)
            ais_a.insert(ais_pre[i]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = ais_a.insert(777);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && ais_a.size() == 5 && ais_a.valid() == 1 &&
             ais_a.contains(777) == 0;
        for (i = 0; i < 5; i++)
            if (!ais_a.contains(ais_pre[i]))
                ok = 0;
        checkn("ais_o", N, ok);
        ais_a.drop();

        {
            intset ais_b = intset();
            for (i = 0; i < 5; i++)
                ais_b.insert(ais_pre[i]);
            stdlib_alloc_fail_reset();
            rc = ais_b.insert(777);
            ok = rc == 0 && ais_b.size() == 6 && ais_b.contains(777) == 1;
            checkn("ais_r", N, ok);
            ais_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        checkn("ais_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

/* ---- vector growth / shrink / copy_from ---------------------------------- */
static void a_vector_grow(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;

    stdlib_alloc_fail_reset();
    {
        vector avg_probe = vector();
        for (i = 0; i < 4; i++)
            avg_probe.push_back(i * 10);
        stdlib_alloc_fail_reset();
        rc = avg_probe.push_back(999);
        seqc = stdlib_alloc_seq();
        check("avg_probe", rc == 0 && seqc == 1 && avg_probe.size() == 5);
        avg_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        vector avg_a = vector();
        stdlib_telemetry_reset();
        for (i = 0; i < 4; i++)
            avg_a.push_back(i * 10);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = avg_a.push_back(999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && avg_a.size() == 4 && avg_a.capacity() == 4;
        for (i = 0; i < 4; i++)
            if (avg_a.at_checked((size_t)i, &o) != 0 || o != i * 10)
                ok = 0;
        checkn("avg_o", N, ok);
        avg_a.drop();
        {
            vector avg_b = vector();
            for (i = 0; i < 4; i++)
                avg_b.push_back(i * 10);
            stdlib_alloc_fail_reset();
            rc = avg_b.push_back(999);
            ok = rc == 0 && avg_b.size() == 5 &&
                 avg_b.at_checked(4, &o) == 0 && o == 999;
            checkn("avg_r", N, ok);
            avg_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        checkn("avg_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

static void a_vector_shrink(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;

    stdlib_alloc_fail_reset();
    {
        vector avs_probe = vector();
        avs_probe.reserve(64);
        for (i = 0; i < 3; i++)
            avs_probe.push_back(i + 5);
        stdlib_alloc_fail_reset();
        rc = avs_probe.shrink_to_fit();
        seqc = stdlib_alloc_seq();
        check("avs_probe", rc == 0 && seqc == 1 && avs_probe.capacity() == 3);
        avs_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        vector avs_a = vector();
        stdlib_telemetry_reset();
        avs_a.reserve(64);
        for (i = 0; i < 3; i++)
            avs_a.push_back(i + 5);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = avs_a.shrink_to_fit();
        stdlib_alloc_fail_reset();
        ok = rc == -1 && avs_a.size() == 3 && avs_a.capacity() == 64;
        for (i = 0; i < 3; i++)
            if (avs_a.at_checked((size_t)i, &o) != 0 || o != i + 5)
                ok = 0;
        checkn("avs_o", N, ok);
        avs_a.drop();
        stdlib_telemetry_snapshot(&tm);
        checkn("avs_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

static void a_vector_copy(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;

    stdlib_alloc_fail_reset();
    {
        vector avc_src_probe = vector();
        vector avc_dst_probe = vector();
        for (i = 0; i < 5; i++)
            avc_src_probe.push_back(i * 11);
        stdlib_alloc_fail_reset();
        rc = avc_dst_probe.copy_from(&avc_src_probe);
        seqc = stdlib_alloc_seq();
        check("avc_probe", rc == 0 && seqc == 1 && avc_dst_probe.size() == 5);
        avc_dst_probe.drop();
        avc_src_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        vector avc_src = vector();
        vector avc_dst = vector();
        stdlib_telemetry_reset();
        for (i = 0; i < 5; i++)
            avc_src.push_back(i * 11);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = avc_dst.copy_from(&avc_src);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && avc_dst.size() == 0 && avc_dst.capacity() == 0 &&
             avc_dst.at_checked(0, &o) == -1;
        checkn("avc_o", N, ok);
        avc_dst.drop();
        stdlib_alloc_fail_reset();
        rc = avc_dst.copy_from(&avc_src);
        ok = rc == 0 && avc_dst.size() == 5 &&
             avc_dst.at_checked(4, &o) == 0 && o == 44;
        checkn("avc_r", N, ok);
        avc_dst.drop();
        avc_src.drop();
        stdlib_telemetry_snapshot(&tm);
        checkn("avc_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

/* ---- deque growth with a wrapped ring ------------------------------------- */
static void a_deque_grow(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;
    int adq_expect[8];
    adq_expect[0] = 2; adq_expect[1] = 3; adq_expect[2] = 4; adq_expect[3] = 5;
    adq_expect[4] = 6; adq_expect[5] = 7; adq_expect[6] = 100; adq_expect[7] = 101;

    stdlib_alloc_fail_reset();
    {
        deque adq_probe = deque();
        for (i = 0; i < 8; i++)
            adq_probe.push_back(i);
        adq_probe.pop_front(&o);
        adq_probe.pop_front(&o);
        adq_probe.push_back(100);
        adq_probe.push_back(101);
        stdlib_alloc_fail_reset();
        rc = adq_probe.push_back(999);
        seqc = stdlib_alloc_seq();
        check("adq_probe", rc == 0 && seqc == 1 && adq_probe.size() == 9 &&
                           adq_probe.capacity() == 16);
        adq_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        deque adq_a = deque();
        stdlib_telemetry_reset();
        for (i = 0; i < 8; i++)
            adq_a.push_back(i);
        adq_a.pop_front(&o);
        adq_a.pop_front(&o);
        adq_a.push_back(100);
        adq_a.push_back(101);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = adq_a.push_back(999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && adq_a.size() == 8 && adq_a.capacity() == 8;
        for (i = 0; i < 8; i++)
            if (adq_a.at_checked((size_t)i, &o) != 0 || o != adq_expect[i])
                ok = 0;
        checkn("adq_o", N, ok);
        adq_a.drop();

        {
            deque adq_b = deque();
            for (i = 0; i < 8; i++)
                adq_b.push_back(i);
            adq_b.pop_front(&o);
            adq_b.pop_front(&o);
            adq_b.push_back(100);
            adq_b.push_back(101);
            stdlib_alloc_fail_reset();
            rc = adq_b.push_back(999);
            ok = rc == 0 && adq_b.size() == 9 &&
                 adq_b.at_checked(8, &o) == 0 && o == 999;
            checkn("adq_r", N, ok);
            adq_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        checkn("adq_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

/* ---- list / stack / queue single-allocation pushes ----------------------- */
static void a_list_push(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;
    int alp_pre[3];
    alp_pre[0] = 11; alp_pre[1] = 22; alp_pre[2] = 33;

    stdlib_alloc_fail_reset();
    {
        list alp_probe = list();
        for (i = 0; i < 3; i++)
            alp_probe.push_back(alp_pre[i]);
        stdlib_alloc_fail_reset();
        rc = alp_probe.push_back(321);
        seqc = stdlib_alloc_seq();
        check("alp_probe", rc == 0 && seqc == 1 && alp_probe.size() == 4);
        alp_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        list alp_a = list();
        stdlib_telemetry_reset();
        for (i = 0; i < 3; i++)
            alp_a.push_back(alp_pre[i]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = alp_a.push_back(321);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && alp_a.size() == 3;
        for (i = 0; i < 3; i++)
            if (alp_a.at_checked((size_t)i, &o) != 0 || o != alp_pre[i])
                ok = 0;
        checkn("alp_o", N, ok);
        alp_a.drop();
        stdlib_telemetry_snapshot(&tm);
        checkn("alp_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

static void a_stack_grow(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;

    stdlib_alloc_fail_reset();
    {
        stack asg_probe = stack();
        for (i = 0; i < 8; i++)
            asg_probe.push(i);
        stdlib_alloc_fail_reset();
        rc = asg_probe.push(654);
        seqc = stdlib_alloc_seq();
        check("asg_probe", rc == 0 && seqc == 1 && asg_probe.size() == 9);
        asg_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        stack asg_a = stack();
        stdlib_telemetry_reset();
        for (i = 0; i < 8; i++)
            asg_a.push(i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = asg_a.push(654);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && asg_a.size() == 8 &&
             asg_a.top_checked(&o) == 0 && o == 7;
        for (i = 0; i < 8; i++)
            if (asg_a.pop(&o) != 0 || o != 7 - i)
                ok = 0;
        checkn("asg_o", N, ok);
        asg_a.drop();
        stdlib_telemetry_snapshot(&tm);
        checkn("asg_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

static void a_queue_grow(void) {
    int N, i, rc, o = 0, ok;
    long seqc;
    stdlib_telemetry_stats tm;
    int aqg_expect[8];
    aqg_expect[0] = 2; aqg_expect[1] = 3; aqg_expect[2] = 4; aqg_expect[3] = 5;
    aqg_expect[4] = 6; aqg_expect[5] = 7; aqg_expect[6] = 100; aqg_expect[7] = 101;

    stdlib_alloc_fail_reset();
    {
        queue aqg_probe = queue();
        for (i = 0; i < 8; i++)
            aqg_probe.push(i);
        aqg_probe.pop(&o);
        aqg_probe.pop(&o);
        aqg_probe.push(100);
        aqg_probe.push(101);
        stdlib_alloc_fail_reset();
        rc = aqg_probe.push(999);
        seqc = stdlib_alloc_seq();
        check("aqg_probe", rc == 0 && seqc == 1 && aqg_probe.size() == 9);
        aqg_probe.drop();
    }

    for (N = 1; N <= seqc; N++) {
        queue aqg_a = queue();
        stdlib_telemetry_reset();
        for (i = 0; i < 8; i++)
            aqg_a.push(i);
        aqg_a.pop(&o);
        aqg_a.pop(&o);
        aqg_a.push(100);
        aqg_a.push(101);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = aqg_a.push(999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && aqg_a.size() == 8;
        for (i = 0; i < 8; i++)
            if (aqg_a.pop(&o) != 0 || o != aqg_expect[i])
                ok = 0;
        checkn("aqg_o", N, ok);
        aqg_a.drop();
        stdlib_telemetry_snapshot(&tm);
        checkn("aqg_l", N, tm.alloc_count == tm.free_count &&
                             tm.alloc_failures >= 1);
    }
}

/* ---- destruction after partial initialisation (owned keys must be freed) - */
static void a_partial_drop(void) {
    int i, o = 0;
    stdlib_telemetry_stats tm;

    /* strmap: 3 rebuild buffers + 2 owned keys succeed, 3rd key copy fails. */
    stdlib_telemetry_reset();
    {
        strmap apd_sm = strmap();
        char apd_k[16];
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(6);
        for (i = 0; i < 20; i++) {
            snprintf(apd_k, sizeof(apd_k), "part%d", i);
            if (apd_sm.put(apd_k, i) != 0)
                break; /* one-shot injection: stop at the failed key */
        }
        stdlib_alloc_fail_reset();
        check("apd_sm_partial", apd_sm.size() == 2 && apd_sm.valid() == 1);
        snprintf(apd_k, sizeof(apd_k), "part1");
        check("apd_sm_value", apd_sm.get(apd_k, &o) == 0 && o == 1);
        apd_sm.drop();
    }
    stdlib_telemetry_snapshot(&tm);
    check("apd_sm_leak", tm.alloc_count == tm.free_count && tm.alloc_count > 0 &&
                         tm.alloc_failures >= 1);

    /* intmap: first rebuild succeeds, second rebuild's 2nd buffer fails. */
    stdlib_telemetry_reset();
    {
        intmap apd_im = intmap();
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(5);
        for (i = 0; i < 20; i++)
            if (apd_im.put(i, i * 2) != 0)
                break; /* one-shot injection: stop at the failed key */
        stdlib_alloc_fail_reset();
        check("apd_im_partial", apd_im.size() == 5 && apd_im.valid() == 1);
        check("apd_im_value", apd_im.get(4, &o) == 0 && o == 8);
        apd_im.drop();
    }
    stdlib_telemetry_snapshot(&tm);
    check("apd_im_leak", tm.alloc_count == tm.free_count && tm.alloc_count > 0 &&
                         tm.alloc_failures >= 1);
}

/* ---- double destruction: every drop() must be idempotent and balanced ---- */
static void a_double_drop(void) {
    int i, o = 0;
    stdlib_telemetry_stats tm;
    stdlib_telemetry_reset();

    {
        vector add_vec = vector();
        intmap add_im = intmap();
        strmap add_sm = strmap();
        intset add_is = intset();
        deque add_dq = deque();
        list add_ls = list();
        stack add_st = stack();
        queue add_qu = queue();

        for (i = 0; i < 12; i++)
            add_vec.push_back(i);
        for (i = 0; i < 12; i++) {
            add_im.put(i, i);
            add_is.insert(i);
            add_dq.push_back(i);
            add_ls.push_back(i);
            add_st.push(i);
            add_qu.push(i);
        }
        add_sm.put("alpha", 1);
        add_sm.put("beta", 2);

        add_vec.drop(); add_vec.drop();
        add_im.drop(); add_im.drop();
        add_sm.drop(); add_sm.drop();
        add_is.drop(); add_is.drop();
        add_dq.drop(); add_dq.drop();
        add_ls.drop(); add_ls.drop();
        add_st.drop(); add_st.drop();
        add_qu.drop(); add_qu.drop();

        check("add_state", add_vec.size() == 0 && add_vec.capacity() == 0 &&
                           add_im.size() == 0 && add_im.valid() == 0 &&
                           add_sm.size() == 0 && add_sm.valid() == 0 &&
                           add_is.size() == 0 && add_is.valid() == 0 &&
                           add_dq.size() == 0 && add_dq.capacity() == 0 &&
                           add_ls.size() == 0 && add_st.size() == 0 &&
                           add_qu.size() == 0);
    }
    stdlib_telemetry_snapshot(&tm);
    check("add_no_double_free", tm.alloc_count == tm.free_count &&
                                tm.alloc_count > 0 && tm.alloc_failures == 0);
    (void)o;
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_alloc_fail_reset();

    a_intmap_grow();
    a_strmap_grow();
    a_intset_grow();
    a_vector_grow();
    a_vector_shrink();
    a_vector_copy();
    a_deque_grow();
    a_list_push();
    a_stack_grow();
    a_queue_grow();
    a_partial_drop();
    a_double_drop();

    stdlib_alloc_fail_reset();
    check("a_terminated", 1);
    stdlib_telemetry_set_enabled(0);
    print("CONTAINER_SAFETY", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
