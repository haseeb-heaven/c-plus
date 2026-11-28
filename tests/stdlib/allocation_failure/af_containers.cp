/* Deterministic allocation-failure injection suite -- container growth.
 *
 * For every fallible growth point of `vector` and ALL seven `collections.hp`
 * containers this fixture:
 *   1. PROBE the number of stdlib_alloc calls on a fresh identical container
 *      (stdlib_alloc_seq() after a successful run);
 *   2. for N = 1..count inject stdlib_alloc_fail_at(N) on a fresh identical
 *      container and require rc == -1 with size/capacity/valid() and every
 *      pre-existing element unchanged and the new element absent;
 *   3. after stdlib_alloc_fail_reset() require the SAME operation to succeed
 *      on a fresh identical container (retry);
 *   4. require telemetry alloc_count == free_count after drop() and
 *      alloc_failures >= 1 (the injection really fired).
 * Injection is a logical counter: no physical memory is exhausted.
 *
 * STRICT-SAFE PATTERNS. Every container variable has a UNIQUE name per function
 * (C+ lowers a method call by the owning variable's name in scope). All
 * out-parameters are initialised at declaration. Telemetry is reset BEFORE the
 * container is built so every counted allocation is also freed in the window.
 *
 * Output: "<tag> <1|0>" per check, then "ALLOC_FAIL <checks> <failures>".
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

static int ac_checks = 0;
static int ac_failures = 0;

static void ac_ck(const char *tag, int cond) {
    ac_checks++;
    if (!cond)
        ac_failures++;
    print(tag, cond ? 1 : 0);
}

static void ac_ckn(const char *prefix, int n, int cond) {
    char ac_lbl[48];
    snprintf(ac_lbl, sizeof(ac_lbl), "%s%d", prefix, n);
    ac_ck(ac_lbl, cond);
}

/* ---- vector: push_back growth, reserve, shrink, copy_from ---------------- */
static void ac_vec_grow(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;

    stdlib_alloc_fail_reset();
    {
        vector acvg_probe = vector();
        for (i = 0; i < 4; i++)
            acvg_probe.push_back(i * 10);
        stdlib_alloc_fail_reset();
        rc = acvg_probe.push_back(999); /* cap 4 -> 8 */
        seqc = stdlib_alloc_seq();
        ac_ck("acvg_probe", rc == 0 && seqc == 1 && acvg_probe.size() == 5);
        acvg_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        vector acvg_a = vector();
        stdlib_telemetry_reset();
        for (i = 0; i < 4; i++)
            acvg_a.push_back(i * 10);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acvg_a.push_back(999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acvg_a.size() == 4 && acvg_a.capacity() == 4;
        for (i = 0; i < 4; i++)
            if (acvg_a.at_checked((size_t)i, &o) != 0 || o != i * 10)
                ok = 0;
        ac_ckn("acvg_o", N, ok);
        acvg_a.drop();
        {
            vector acvg_b = vector();
            for (i = 0; i < 4; i++)
                acvg_b.push_back(i * 10);
            stdlib_alloc_fail_reset();
            rc = acvg_b.push_back(999);
            o = 0;
            ok = rc == 0 && acvg_b.size() == 5 &&
                 acvg_b.at_checked(4, &o) == 0 && o == 999;
            ac_ckn("acvg_r", N, ok);
            acvg_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acvg_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

static void ac_vec_shrink(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;

    stdlib_alloc_fail_reset();
    {
        vector acvs_probe = vector();
        acvs_probe.reserve(64);
        for (i = 0; i < 3; i++)
            acvs_probe.push_back(i + 5);
        stdlib_alloc_fail_reset();
        rc = acvs_probe.shrink_to_fit();
        seqc = stdlib_alloc_seq();
        ac_ck("acvs_probe", rc == 0 && seqc == 1 && acvs_probe.capacity() == 3);
        acvs_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        vector acvs_a = vector();
        stdlib_telemetry_reset();
        acvs_a.reserve(64);
        for (i = 0; i < 3; i++)
            acvs_a.push_back(i + 5);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acvs_a.shrink_to_fit();
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acvs_a.size() == 3 && acvs_a.capacity() == 64;
        for (i = 0; i < 3; i++)
            if (acvs_a.at_checked((size_t)i, &o) != 0 || o != i + 5)
                ok = 0;
        ac_ckn("acvs_o", N, ok);
        acvs_a.drop();
        {
            vector acvs_b = vector();
            acvs_b.reserve(64);
            for (i = 0; i < 3; i++)
                acvs_b.push_back(i + 5);
            stdlib_alloc_fail_reset();
            rc = acvs_b.shrink_to_fit();
            ok = rc == 0 && acvs_b.capacity() == 3 && acvs_b.size() == 3;
            ac_ckn("acvs_r", N, ok);
            acvs_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acvs_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

static void ac_vec_copy(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;

    stdlib_alloc_fail_reset();
    {
        vector acvc_src_probe = vector();
        vector acvc_dst_probe = vector();
        for (i = 0; i < 5; i++)
            acvc_src_probe.push_back(i * 11);
        stdlib_alloc_fail_reset();
        rc = acvc_dst_probe.copy_from(&acvc_src_probe);
        seqc = stdlib_alloc_seq();
        ac_ck("acvc_probe", rc == 0 && seqc == 1 && acvc_dst_probe.size() == 5);
        acvc_dst_probe.drop();
        acvc_src_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        vector acvc_src = vector();
        vector acvc_dst = vector();
        stdlib_telemetry_reset();
        for (i = 0; i < 5; i++)
            acvc_src.push_back(i * 11);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acvc_dst.copy_from(&acvc_src);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acvc_dst.size() == 0 && acvc_dst.capacity() == 0 &&
             acvc_dst.at_checked(0, &o) == -1;
        ac_ckn("acvc_o", N, ok);
        acvc_dst.drop();
        {
            vector acvc_b = vector();
            stdlib_alloc_fail_reset();
            rc = acvc_b.copy_from(&acvc_src);
            o = 0;
            ok = rc == 0 && acvc_b.size() == 5 &&
                 acvc_b.at_checked(4, &o) == 0 && o == 44;
            ac_ckn("acvc_r", N, ok);
            acvc_b.drop();
        }
        acvc_src.drop();
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acvc_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

/* ---- intmap: three-buffer rebuild ---------------------------------------- */
static void ac_intmap(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;
    int acim_pre[5];
    acim_pre[0] = 10; acim_pre[1] = 20; acim_pre[2] = 30;
    acim_pre[3] = 40; acim_pre[4] = 50;

    stdlib_alloc_fail_reset();
    {
        intmap acim_probe = intmap();
        for (i = 0; i < 5; i++)
            acim_probe.put(acim_pre[i], i);
        stdlib_alloc_fail_reset();
        rc = acim_probe.put(100, 999);
        seqc = stdlib_alloc_seq();
        ac_ck("acim_probe", rc == 0 && seqc >= 1 && seqc <= 3);
        acim_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        intmap acim_a = intmap();
        stdlib_telemetry_reset();
        for (i = 0; i < 5; i++)
            acim_a.put(acim_pre[i], i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acim_a.put(100, 999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acim_a.size() == 5 && acim_a.valid() == 1 &&
             acim_a.contains(100) == 0;
        for (i = 0; i < 5; i++)
            if (acim_a.get(acim_pre[i], &o) != 0 || o != i)
                ok = 0;
        ac_ckn("acim_o", N, ok);
        acim_a.drop();
        {
            intmap acim_b = intmap();
            for (i = 0; i < 5; i++)
                acim_b.put(acim_pre[i], i);
            stdlib_alloc_fail_reset();
            rc = acim_b.put(100, 999);
            ok = rc == 0 && acim_b.size() == 6 &&
                 acim_b.get(100, &o) == 0 && o == 999;
            ac_ckn("acim_r", N, ok);
            acim_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acim_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

/* ---- strmap: three rebuild buffers + one owned-key copy ------------------ */
static void ac_strmap(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;
    char acsm_keys[5][12];

    stdlib_alloc_fail_reset();
    {
        strmap acsm_probe = strmap();
        for (i = 0; i < 5; i++) {
            snprintf(acsm_keys[i], sizeof(acsm_keys[i]), "p%d", i);
            acsm_probe.put(acsm_keys[i], i);
        }
        stdlib_alloc_fail_reset();
        rc = acsm_probe.put("fresh", 999);
        seqc = stdlib_alloc_seq();
        ac_ck("acsm_probe", rc == 0 && seqc >= 1 && seqc <= 4);
        acsm_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        strmap acsm_a = strmap();
        stdlib_telemetry_reset();
        for (i = 0; i < 5; i++) {
            snprintf(acsm_keys[i], sizeof(acsm_keys[i]), "p%d", i);
            acsm_a.put(acsm_keys[i], i);
        }
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acsm_a.put("fresh", 999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acsm_a.size() == 5 && acsm_a.valid() == 1 &&
             acsm_a.contains("fresh") == 0;
        for (i = 0; i < 5; i++) {
            snprintf(acsm_keys[i], sizeof(acsm_keys[i]), "p%d", i);
            if (acsm_a.get(acsm_keys[i], &o) != 0 || o != i)
                ok = 0;
        }
        ac_ckn("acsm_o", N, ok);
        acsm_a.drop();
        {
            strmap acsm_b = strmap();
            for (i = 0; i < 5; i++) {
                snprintf(acsm_keys[i], sizeof(acsm_keys[i]), "p%d", i);
                acsm_b.put(acsm_keys[i], i);
            }
            stdlib_alloc_fail_reset();
            rc = acsm_b.put("fresh", 999);
            ok = rc == 0 && acsm_b.size() == 6 &&
                 acsm_b.get("fresh", &o) == 0 && o == 999;
            ac_ckn("acsm_r", N, ok);
            acsm_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acsm_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

/* ---- intset: two-buffer rebuild ------------------------------------------ */
static void ac_intset(void) {
    int N, i, rc, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;
    int acis_pre[5];
    acis_pre[0] = 11; acis_pre[1] = 22; acis_pre[2] = 33;
    acis_pre[3] = 44; acis_pre[4] = 55;

    stdlib_alloc_fail_reset();
    {
        intset acis_probe = intset();
        for (i = 0; i < 5; i++)
            acis_probe.insert(acis_pre[i]);
        stdlib_alloc_fail_reset();
        rc = acis_probe.insert(777);
        seqc = stdlib_alloc_seq();
        ac_ck("acis_probe", rc == 0 && seqc >= 1 && seqc <= 2);
        acis_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        intset acis_a = intset();
        stdlib_telemetry_reset();
        for (i = 0; i < 5; i++)
            acis_a.insert(acis_pre[i]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acis_a.insert(777);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acis_a.size() == 5 && acis_a.valid() == 1 &&
             acis_a.contains(777) == 0;
        for (i = 0; i < 5; i++)
            if (!acis_a.contains(acis_pre[i]))
                ok = 0;
        ac_ckn("acis_o", N, ok);
        acis_a.drop();
        {
            intset acis_b = intset();
            for (i = 0; i < 5; i++)
                acis_b.insert(acis_pre[i]);
            stdlib_alloc_fail_reset();
            rc = acis_b.insert(777);
            ok = rc == 0 && acis_b.size() == 6 && acis_b.contains(777) == 1;
            ac_ckn("acis_r", N, ok);
            acis_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acis_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

/* ---- deque: ring copy on growth of a full wrapped ring ------------------- */
static void ac_deque(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;
    int acdq_expect[8];
    acdq_expect[0] = 2; acdq_expect[1] = 3; acdq_expect[2] = 4;
    acdq_expect[3] = 5; acdq_expect[4] = 6; acdq_expect[5] = 7;
    acdq_expect[6] = 100; acdq_expect[7] = 101;

    stdlib_alloc_fail_reset();
    {
        deque acdq_probe = deque();
        for (i = 0; i < 8; i++)
            acdq_probe.push_back(i);
        acdq_probe.pop_front(&o);
        acdq_probe.pop_front(&o);
        acdq_probe.push_back(100);
        acdq_probe.push_back(101);
        stdlib_alloc_fail_reset();
        rc = acdq_probe.push_back(999);
        seqc = stdlib_alloc_seq();
        ac_ck("acdq_probe", rc == 0 && seqc == 1 &&
                           acdq_probe.size() == 9 && acdq_probe.capacity() == 16);
        acdq_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        deque acdq_a = deque();
        stdlib_telemetry_reset();
        for (i = 0; i < 8; i++)
            acdq_a.push_back(i);
        acdq_a.pop_front(&o);
        acdq_a.pop_front(&o);
        acdq_a.push_back(100);
        acdq_a.push_back(101);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acdq_a.push_back(999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acdq_a.size() == 8 && acdq_a.capacity() == 8;
        for (i = 0; i < 8; i++)
            if (acdq_a.at_checked((size_t)i, &o) != 0 || o != acdq_expect[i])
                ok = 0;
        ac_ckn("acdq_o", N, ok);
        acdq_a.drop();
        {
            deque acdq_b = deque();
            for (i = 0; i < 8; i++)
                acdq_b.push_back(i);
            acdq_b.pop_front(&o);
            acdq_b.pop_front(&o);
            acdq_b.push_back(100);
            acdq_b.push_back(101);
            stdlib_alloc_fail_reset();
            rc = acdq_b.push_back(999);
            o = 0;
            ok = rc == 0 && acdq_b.size() == 9 &&
                 acdq_b.at_checked(8, &o) == 0 && o == 999;
            ac_ckn("acdq_r", N, ok);
            acdq_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acdq_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

/* ---- list: one node allocation per push ---------------------------------- */
static void ac_list(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;
    int aclsp_pre[3];
    aclsp_pre[0] = 11; aclsp_pre[1] = 22; aclsp_pre[2] = 33;

    stdlib_alloc_fail_reset();
    {
        list aclsp_probe = list();
        for (i = 0; i < 3; i++)
            aclsp_probe.push_back(aclsp_pre[i]);
        stdlib_alloc_fail_reset();
        rc = aclsp_probe.push_back(321);
        seqc = stdlib_alloc_seq();
        ac_ck("aclsp_probe", rc == 0 && seqc == 1 && aclsp_probe.size() == 4);
        aclsp_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        list aclsp_a = list();
        stdlib_telemetry_reset();
        for (i = 0; i < 3; i++)
            aclsp_a.push_back(aclsp_pre[i]);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = aclsp_a.push_back(321);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && aclsp_a.size() == 3;
        for (i = 0; i < 3; i++)
            if (aclsp_a.at_checked((size_t)i, &o) != 0 || o != aclsp_pre[i])
                ok = 0;
        ac_ckn("aclsp_o", N, ok);
        aclsp_a.drop();
        {
            list aclsp_b = list();
            for (i = 0; i < 3; i++)
                aclsp_b.push_back(aclsp_pre[i]);
            stdlib_alloc_fail_reset();
            rc = aclsp_b.push_back(321);
            o = 0;
            ok = rc == 0 && aclsp_b.size() == 4 &&
                 aclsp_b.at_checked(3, &o) == 0 && o == 321;
            ac_ckn("aclsp_r", N, ok);
            aclsp_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("aclsp_l", N, tm.alloc_count == tm.free_count &&
                              tm.alloc_failures >= 1);
    }
}

/* ---- stack: one int-buffer allocation on growth -------------------------- */
static void ac_stack(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;

    stdlib_alloc_fail_reset();
    {
        stack acst_probe = stack();
        for (i = 0; i < 8; i++)
            acst_probe.push(i);
        stdlib_alloc_fail_reset();
        rc = acst_probe.push(654);
        seqc = stdlib_alloc_seq();
        ac_ck("acst_probe", rc == 0 && seqc == 1 && acst_probe.size() == 9);
        acst_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        stack acst_a = stack();
        stdlib_telemetry_reset();
        for (i = 0; i < 8; i++)
            acst_a.push(i);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acst_a.push(654);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acst_a.size() == 8 &&
             acst_a.top_checked(&o) == 0 && o == 7;
        for (i = 0; i < 8; i++)
            if (acst_a.pop(&o) != 0 || o != 7 - i)
                ok = 0;
        ac_ckn("acst_o", N, ok);
        acst_a.drop();
        {
            stack acst_b = stack();
            for (i = 0; i < 8; i++)
                acst_b.push(i);
            stdlib_alloc_fail_reset();
            rc = acst_b.push(654);
            ok = rc == 0 && acst_b.size() == 9 &&
                 acst_b.top_checked(&o) == 0 && o == 654;
            ac_ckn("acst_r", N, ok);
            acst_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acst_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

/* ---- queue: ring-copy allocation on growth of a full wrapped ring -------- */
static void ac_queue(void) {
    int N, i, rc, o = 0, ok;
    long seqc = 0;
    stdlib_telemetry_stats tm;
    int acqu_expect[8];
    acqu_expect[0] = 2; acqu_expect[1] = 3; acqu_expect[2] = 4;
    acqu_expect[3] = 5; acqu_expect[4] = 6; acqu_expect[5] = 7;
    acqu_expect[6] = 100; acqu_expect[7] = 101;

    stdlib_alloc_fail_reset();
    {
        queue acqu_probe = queue();
        for (i = 0; i < 8; i++)
            acqu_probe.push(i);
        acqu_probe.pop(&o);
        acqu_probe.pop(&o);
        acqu_probe.push(100);
        acqu_probe.push(101);
        stdlib_alloc_fail_reset();
        rc = acqu_probe.push(999);
        seqc = stdlib_alloc_seq();
        ac_ck("acqu_probe", rc == 0 && seqc == 1 && acqu_probe.size() == 9);
        acqu_probe.drop();
    }
    for (N = 1; N <= seqc; N++) {
        queue acqu_a = queue();
        stdlib_telemetry_reset();
        for (i = 0; i < 8; i++)
            acqu_a.push(i);
        acqu_a.pop(&o);
        acqu_a.pop(&o);
        acqu_a.push(100);
        acqu_a.push(101);
        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(N);
        rc = acqu_a.push(999);
        stdlib_alloc_fail_reset();
        ok = rc == -1 && acqu_a.size() == 8;
        for (i = 0; i < 8; i++)
            if (acqu_a.pop(&o) != 0 || o != acqu_expect[i])
                ok = 0;
        ac_ckn("acqu_o", N, ok);
        acqu_a.drop();
        {
            queue acqu_b = queue();
            for (i = 0; i < 8; i++)
                acqu_b.push(i);
            acqu_b.pop(&o);
            acqu_b.pop(&o);
            acqu_b.push(100);
            acqu_b.push(101);
            stdlib_alloc_fail_reset();
            rc = acqu_b.push(999);
            ok = rc == 0 && acqu_b.size() == 9 &&
                 acqu_b.front_checked(&o) == 0 && o == 2;
            ac_ckn("acqu_r", N, ok);
            acqu_b.drop();
        }
        stdlib_telemetry_snapshot(&tm);
        ac_ckn("acqu_l", N, tm.alloc_count == tm.free_count &&
                            tm.alloc_failures >= 1);
    }
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_alloc_fail_reset();

    ac_vec_grow();
    ac_vec_shrink();
    ac_vec_copy();
    ac_intmap();
    ac_strmap();
    ac_intset();
    ac_deque();
    ac_list();
    ac_stack();
    ac_queue();

    stdlib_alloc_fail_reset();
    print("ALLOC_FAIL", ac_checks, ac_failures);
    stdlib_telemetry_set_enabled(0);
    return ac_failures ? 1 : 0;
}
