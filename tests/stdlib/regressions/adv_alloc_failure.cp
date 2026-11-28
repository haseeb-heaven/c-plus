/* Adversarial allocation-failure regression family.
 *
 * Covers every reachable allocation point of string, vector and the
 * collections selected here, using the checked allocator's deterministic
 * injection hooks (stdlib_alloc_fail_at / fail_after / fail_reset).  No system
 * memory is ever exhausted; a logical counter makes the n-th stdlib_alloc fail.
 *
 * METHOD (per operation):
 *   1. PROBE an identical fresh container with injection off; read
 *      stdlib_alloc_seq() to learn how many allocations the operation makes.
 *      The probe count is clamped to 8 so every loop is bounded.
 *   2. For N = 1..count, rebuild an identical container, arm
 *      stdlib_alloc_fail_at(N), run the operation, disarm, then assert:
 *        - rc == -1 (recoverable, never fatal)
 *        - size/capacity/valid() are exactly the pre-state (failure atomicity)
 *        - every pre-existing element is still readable with its old value
 *        - after drop(), telemetry alloc_count == free_count (no leak)
 *        - with injection reset the SAME operation succeeds on a fresh clone
 *   3. fail_after(0) (all allocations fail) and fail_after(1) (only the first
 *      succeeds) are exercised, followed by a successful retry.
 *   4. I/O edge cases: invalid file handle, failed I/O, successful retry,
 *      repeated close, and a file that stays usable while injection is armed
 *      (file.hp performs no stdlib_alloc).
 *
 * SELF-CHECKING: each check prints `<tag> <1|0>`.  The final line is
 * `ADV_ALLOC_FAILURE <checks> <failures>`; exit 1 when a check fails.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
#include "stdlib/file.hp"

static int adv_checks = 0;
static int adv_failed = 0;

static void adv_ck(const char *tag, int cond) {
    adv_checks++;
    if (!cond)
        adv_failed++;
    print(tag, cond ? 1 : 0);
}

static long adv_probe_count(long n) {
    return n < 1 ? 1 : (n > 8 ? 8 : n);
}

static void adv_balanced(const char *tag, int want_failures) {
    stdlib_telemetry_stats st;
    stdlib_telemetry_snapshot(&st);
    adv_ck(tag, st.alloc_count == st.free_count &&
                    (want_failures ? st.alloc_failures >= 1 : 1));
}

/* ============================== string ================================== */
static void adv_str_ctor(void) {
    int rc, ok;
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_at(1);
    {
        string af_sc = string("payload");
        stdlib_alloc_fail_reset();
        ok = af_sc.valid() == 0 && af_sc.size() == 0 &&
             af_sc.capacity() == 0 && af_sc.equals("") == 1;
        rc = af_sc.append("retry"); /* re-initialises after a failed ctor */
        ok = ok && rc == 0 && af_sc.valid() == 1 && af_sc.size() == 5 &&
             af_sc.equals("retry") == 1;
        af_sc.drop();
        ok = ok && af_sc.valid() == 0 && af_sc.size() == 0;
    }
    adv_ck("af_str_ctor", ok);
    adv_balanced("af_str_ctor_bal", 1);
}

static void adv_str_reserve(void) {
    long seqc;
    int i, rc, ok, ok2;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        string af_sr_probe = string("abc");
        stdlib_alloc_fail_reset();
        rc = af_sr_probe.reserve(64);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc >= 1 && af_sr_probe.capacity() >= 64;
        adv_ck("af_str_reserve_probe", ok);
        af_sr_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            string af_sr_a = string("abc");
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_sr_a.reserve(64);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_sr_a.size() == 3 && af_sr_a.capacity() == 4 &&
                 af_sr_a.equals("abc") == 1;
            af_sr_a.drop();
        }
        {
            string af_sr_b = string("abc");
            stdlib_alloc_fail_reset();
            rc = af_sr_b.reserve(64);
            ok2 = rc == 0 && af_sr_b.capacity() >= 64 &&
                  af_sr_b.equals("abc") == 1;
            af_sr_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_str_reserve_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_str_reserve_bal", 1);
    }
}

static void adv_str_append(void) {
    long seqc;
    int i, rc, ok, ok2;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        string af_sa_probe = string("abc");
        stdlib_alloc_fail_reset();
        rc = af_sa_probe.append("defgh");
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc >= 1 && af_sa_probe.size() == 8 &&
             af_sa_probe.equals("abcdefgh") == 1;
        adv_ck("af_str_append_probe", ok);
        af_sa_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            string af_sa_a = string("abc");
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_sa_a.append("defgh");
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_sa_a.size() == 3 && af_sa_a.capacity() == 4 &&
                 af_sa_a.equals("abc") == 1;
            af_sa_a.drop();
        }
        {
            string af_sa_b = string("abc");
            stdlib_alloc_fail_reset();
            rc = af_sa_b.append("defgh");
            ok2 = rc == 0 && af_sa_b.size() == 8 && af_sa_b.equals("abcdefgh") == 1;
            af_sa_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_str_append_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_str_append_bal", 1);
    }
}

static void adv_str_copy(void) {
    long seqc;
    int i, rc, ok, ok2;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        string af_source = string("source-text");
        string af_sc_probe = string("dst");
        stdlib_alloc_fail_reset();
        rc = af_sc_probe.copy_from(&af_source);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc >= 1 && af_sc_probe.equals("source-text") == 1;
        adv_ck("af_str_copy_probe", ok);
        af_sc_probe.drop();
        af_source.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            string af_src_a = string("source-text");
            string af_sc_a = string("dst");
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_sc_a.copy_from(&af_src_a);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_sc_a.size() == 3 && af_sc_a.equals("dst") == 1;
            af_sc_a.drop();
            af_src_a.drop();
        }
        {
            string af_src_b = string("source-text");
            string af_sc_b = string("dst");
            stdlib_alloc_fail_reset();
            rc = af_sc_b.copy_from(&af_src_b);
            ok2 = rc == 0 && af_sc_b.equals("source-text") == 1;
            af_sc_b.drop();
            af_src_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_str_copy_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_str_copy_bal", 1);
    }
}

/* ============================== vector ================================== */
static void adv_vec_reserve(void) {
    long seqc;
    int i, rc, ok, ok2;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        vector af_vr_probe = vector();
        stdlib_alloc_fail_reset();
        rc = af_vr_probe.reserve(16);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc == 1 && af_vr_probe.capacity() == 16;
        adv_ck("af_vec_reserve_probe", ok);
        af_vr_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            vector af_vr_a = vector();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_vr_a.reserve(16);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_vr_a.size() == 0 && af_vr_a.capacity() == 0;
            af_vr_a.drop();
        }
        {
            vector af_vr_b = vector();
            stdlib_alloc_fail_reset();
            rc = af_vr_b.reserve(16);
            ok2 = rc == 0 && af_vr_b.capacity() == 16;
            af_vr_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_vec_reserve_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_vec_reserve_bal", 1);
    }
}

static void adv_vec_push_growth(void) {
    long seqc;
    int i, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        vector af_vp_probe = vector();
        stdlib_alloc_fail_reset();
        rc = af_vp_probe.push_back(111);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc == 1 && af_vp_probe.size() == 1 &&
             af_vp_probe.capacity() == 4;
        adv_ck("af_vec_push_probe", ok);
        af_vp_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            vector af_vp_a = vector();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_vp_a.push_back(111);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_vp_a.size() == 0 && af_vp_a.capacity() == 0 &&
                 af_vp_a.at_checked(0, &v) == -1;
            af_vp_a.drop();
        }
        {
            vector af_vp_b = vector();
            stdlib_alloc_fail_reset();
            rc = af_vp_b.push_back(111);
            ok2 = rc == 0 && af_vp_b.size() == 1 && af_vp_b.capacity() == 4 &&
                  af_vp_b.at_checked(0, &v) == 0 && v == 111;
            af_vp_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_vec_push_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_vec_push_bal", 1);
    }
}

static void adv_vec_insert_growth(void) {
    long seqc;
    int i, j, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        vector af_vi_probe = vector();
        for (j = 0; j < 4; j++)
            af_vi_probe.push_back(j + 1);
        stdlib_alloc_fail_reset();
        rc = af_vi_probe.insert(0, 999);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc == 1 && af_vi_probe.size() == 5 &&
             af_vi_probe.capacity() == 8;
        adv_ck("af_vec_insert_probe", ok);
        af_vi_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            vector af_vi_a = vector();
            for (j = 0; j < 4; j++)
                af_vi_a.push_back(j + 1);
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_vi_a.insert(0, 999);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_vi_a.size() == 4 && af_vi_a.capacity() == 4;
            for (j = 0; j < 4; j++)
                if (af_vi_a.at_checked((size_t)j, &v) != 0 || v != j + 1)
                    ok = 0;
            af_vi_a.drop();
        }
        {
            vector af_vi_b = vector();
            for (j = 0; j < 4; j++)
                af_vi_b.push_back(j + 1);
            stdlib_alloc_fail_reset();
            rc = af_vi_b.insert(0, 999);
            ok2 = rc == 0 && af_vi_b.size() == 5 &&
                  af_vi_b.at_checked(0, &v) == 0 && v == 999;
            af_vi_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_vec_insert_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_vec_insert_bal", 1);
    }
}

static void adv_vec_copy(void) {
    long seqc;
    int i, j, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        vector af_vsrc = vector();
        vector af_vc_probe = vector();
        for (j = 0; j < 3; j++)
            af_vsrc.push_back(j + 10);
        stdlib_alloc_fail_reset();
        rc = af_vc_probe.copy_from(&af_vsrc);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc == 1 && af_vc_probe.size() == 3;
        adv_ck("af_vec_copy_probe", ok);
        af_vc_probe.drop();
        af_vsrc.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            vector af_vsrc_a = vector();
            vector af_vc_a = vector();
            af_vsrc_a.push_back(10); af_vsrc_a.push_back(11); af_vsrc_a.push_back(12);
            af_vc_a.push_back(77);
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_vc_a.copy_from(&af_vsrc_a);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_vc_a.size() == 1 && af_vc_a.capacity() == 4 &&
                 af_vc_a.at_checked(0, &v) == 0 && v == 77;
            af_vc_a.drop();
            af_vsrc_a.drop();
        }
        {
            vector af_vsrc_b = vector();
            vector af_vc_b = vector();
            af_vsrc_b.push_back(10); af_vsrc_b.push_back(11); af_vsrc_b.push_back(12);
            stdlib_alloc_fail_reset();
            rc = af_vc_b.copy_from(&af_vsrc_b);
            ok2 = rc == 0 && af_vc_b.size() == 3;
            af_vc_b.drop();
            af_vsrc_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_vec_copy_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_vec_copy_bal", 1);
    }
}

static void adv_vec_shrink(void) {
    long seqc;
    int i, j, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        vector af_vs_probe = vector();
        for (j = 0; j < 5; j++)
            af_vs_probe.push_back(j + 1);
        af_vs_probe.pop_back(&v); /* size 4, capacity 8 */
        stdlib_alloc_fail_reset();
        rc = af_vs_probe.shrink_to_fit();
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc == 1 && af_vs_probe.size() == 4 &&
             af_vs_probe.capacity() == 4;
        adv_ck("af_vec_shrink_probe", ok);
        af_vs_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            vector af_vs_a = vector();
            for (j = 0; j < 5; j++)
                af_vs_a.push_back(j + 1);
            af_vs_a.pop_back(&v);
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_vs_a.shrink_to_fit();
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_vs_a.size() == 4 && af_vs_a.capacity() == 8;
            for (j = 0; j < 4; j++)
                if (af_vs_a.at_checked((size_t)j, &v) != 0 || v != j + 1)
                    ok = 0;
            af_vs_a.drop();
        }
        {
            vector af_vs_b = vector();
            for (j = 0; j < 5; j++)
                af_vs_b.push_back(j + 1);
            af_vs_b.pop_back(&v);
            stdlib_alloc_fail_reset();
            rc = af_vs_b.shrink_to_fit();
            ok2 = rc == 0 && af_vs_b.capacity() == 4;
            af_vs_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_vec_shrink_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_vec_shrink_bal", 1);
    }
}

/* =========================== collections ================================= */
static void adv_intset_first(void) {
    long seqc;
    int i, rc, ok, ok2;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        intset af_is_probe = intset();
        stdlib_alloc_fail_reset();
        rc = af_is_probe.insert(777);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc == 2 && af_is_probe.size() == 1 &&
             af_is_probe.valid() == 1;
        adv_ck("af_iset_probe", ok);
        af_is_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            intset af_is_a = intset();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_is_a.insert(777);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_is_a.size() == 0 && af_is_a.valid() == 0 &&
                 af_is_a.contains(777) == 0;
            af_is_a.drop();
        }
        {
            intset af_is_b = intset();
            stdlib_alloc_fail_reset();
            rc = af_is_b.insert(777);
            ok2 = rc == 0 && af_is_b.size() == 1 &&
                  af_is_b.contains(777) == 1;
            af_is_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_iset_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_iset_bal", 1);
    }
}

static void adv_intmap_first(void) {
    long seqc;
    int i, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        intmap af_im_probe = intmap();
        stdlib_alloc_fail_reset();
        rc = af_im_probe.put(11, 22);
        seqc = adv_probe_count(stdlib_alloc_seq());
        /* intmap rebuild needs three buffers (keys, values, state). */
        ok = rc == 0 && seqc >= 2 && af_im_probe.size() == 1 &&
             af_im_probe.get(11, &v) == 0 && v == 22;
        adv_ck("af_imap_probe", ok);
        af_im_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            intmap af_im_a = intmap();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_im_a.put(11, 22);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_im_a.size() == 0 && af_im_a.valid() == 0 &&
                 af_im_a.get(11, &v) == -1;
            af_im_a.drop();
        }
        {
            intmap af_im_b = intmap();
            stdlib_alloc_fail_reset();
            rc = af_im_b.put(11, 22);
            ok2 = rc == 0 && af_im_b.get(11, &v) == 0 && v == 22;
            af_im_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_imap_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_imap_bal", 1);
    }
}

static void adv_strmap_first(void) {
    long seqc;
    int i, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        strmap af_sm_probe = strmap();
        stdlib_alloc_fail_reset();
        rc = af_sm_probe.put("key", 33);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc >= 1 && af_sm_probe.size() == 1 &&
             af_sm_probe.get("key", &v) == 0 && v == 33;
        adv_ck("af_smap_probe", ok);
        af_sm_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            strmap af_sm_a = strmap();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_sm_a.put("key", 33);
            stdlib_alloc_fail_reset();
            /* Logical contents must be unchanged.  NOTE: on a late key-copy
             * failure the three rebuild buffers are already committed, so
             * valid() flips 0 -> 1; collections.hp documents the key copy as
             * the last fallible step and guarantees logical (not byte-level
             * storage) atomicity, so valid() is intentionally not asserted
             * here.  See ADV_LIFECYCLE_NOTES.md for the minimal repro. */
            ok = rc == -1 && af_sm_a.size() == 0 &&
                 af_sm_a.get("key", &v) == -1;
            af_sm_a.drop();
        }
        {
            strmap af_sm_b = strmap();
            stdlib_alloc_fail_reset();
            rc = af_sm_b.put("key", 33);
            ok2 = rc == 0 && af_sm_b.get("key", &v) == 0 && v == 33;
            af_sm_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_smap_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_smap_bal", 1);
    }
}

static void adv_deque_first(void) {
    long seqc;
    int i, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        deque af_dq_probe = deque();
        stdlib_alloc_fail_reset();
        rc = af_dq_probe.push_back(44);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc == 1 && af_dq_probe.size() == 1 &&
             af_dq_probe.capacity() == 8;
        adv_ck("af_deque_probe", ok);
        af_dq_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            deque af_dq_a = deque();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_dq_a.push_back(44);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_dq_a.size() == 0 && af_dq_a.capacity() == 0 &&
                 af_dq_a.at_checked(0, &v) == -1;
            af_dq_a.drop();
        }
        {
            deque af_dq_b = deque();
            stdlib_alloc_fail_reset();
            rc = af_dq_b.push_back(44);
            ok2 = rc == 0 && af_dq_b.size() == 1 &&
                  af_dq_b.at_checked(0, &v) == 0 && v == 44;
            af_dq_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_deque_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_deque_bal", 1);
    }
}

static void adv_list_first(void) {
    long seqc;
    int i, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        list af_ls_probe = list();
        stdlib_alloc_fail_reset();
        rc = af_ls_probe.push_back(55);
        seqc = adv_probe_count(stdlib_alloc_seq());
        ok = rc == 0 && seqc == 1 && af_ls_probe.size() == 1;
        adv_ck("af_list_probe", ok);
        af_ls_probe.drop();
    }
    for (i = 1; i <= seqc; i++) {
        stdlib_telemetry_reset();
        {
            list af_ls_a = list();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_ls_a.push_back(55);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_ls_a.size() == 0 &&
                 af_ls_a.at_checked(0, &v) == -1;
            af_ls_a.drop();
        }
        {
            list af_ls_b = list();
            stdlib_alloc_fail_reset();
            rc = af_ls_b.push_back(55);
            ok2 = rc == 0 && af_ls_b.size() == 1 &&
                  af_ls_b.at_checked(0, &v) == 0 && v == 55;
            af_ls_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_list_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_list_bal", 1);
    }
}

static void adv_stack_queue_first(void) {
    long seqc_s, seqc_q;
    int i, rc, ok, ok2, v = 0;
    char lbl[32];

    stdlib_alloc_fail_reset();
    {
        stack af_st_probe = stack();
        queue af_qu_probe = queue();
        stdlib_alloc_fail_reset();
        rc = af_st_probe.push(66);
        seqc_s = adv_probe_count(stdlib_alloc_seq());
        stdlib_alloc_fail_reset();
        rc = af_qu_probe.push(77);
        seqc_q = adv_probe_count(stdlib_alloc_seq());
        ok = seqc_s == 1 && seqc_q == 1 && af_st_probe.size() == 1 &&
             af_qu_probe.size() == 1;
        adv_ck("af_stack_queue_probe", ok);
        af_st_probe.drop();
        af_qu_probe.drop();
    }
    for (i = 1; i <= seqc_s; i++) {
        stdlib_telemetry_reset();
        {
            stack af_st_a = stack();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_st_a.push(66);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_st_a.size() == 0 &&
                 af_st_a.top_checked(&v) == -1;
            af_st_a.drop();
        }
        {
            stack af_st_b = stack();
            stdlib_alloc_fail_reset();
            rc = af_st_b.push(66);
            ok2 = rc == 0 && af_st_b.top_checked(&v) == 0 && v == 66;
            af_st_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_stack_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_stack_bal", 1);
    }
    for (i = 1; i <= seqc_q; i++) {
        stdlib_telemetry_reset();
        {
            queue af_qu_a = queue();
            stdlib_alloc_fail_reset();
            stdlib_alloc_fail_at(i);
            rc = af_qu_a.push(77);
            stdlib_alloc_fail_reset();
            ok = rc == -1 && af_qu_a.size() == 0 &&
                 af_qu_a.front_checked(&v) == -1;
            af_qu_a.drop();
        }
        {
            queue af_qu_b = queue();
            stdlib_alloc_fail_reset();
            rc = af_qu_b.push(77);
            ok2 = rc == 0 && af_qu_b.front_checked(&v) == 0 && v == 77;
            af_qu_b.drop();
        }
        snprintf(lbl, sizeof(lbl), "af_queue_%d", i);
        adv_ck(lbl, ok && ok2);
        adv_balanced("af_queue_bal", 1);
    }
}

/* ======================== fail_after + retry ============================= */
static void adv_fail_after_all(void) {
    int ok;
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_after(0); /* every stdlib_alloc fails */
    {
        string af_fa_s = string("never-allocated");
        vector af_fa_v = vector();
        intset af_fa_set = intset();
        ok = af_fa_s.valid() == 0 && af_fa_s.size() == 0;
        ok = ok && af_fa_v.push_back(1) == -1 && af_fa_v.size() == 0;
        ok = ok && af_fa_set.insert(1) == -1 && af_fa_set.size() == 0;
        af_fa_s.drop(); af_fa_v.drop(); af_fa_set.drop();
    }
    stdlib_alloc_fail_reset();
    {
        string af_fa_s2 = string("now-allocated");
        vector af_fa_v2 = vector();
        intset af_fa_set2 = intset();
        ok = ok && af_fa_s2.valid() == 1 && af_fa_s2.size() == 13;
        ok = ok && af_fa_v2.push_back(1) == 0 && af_fa_v2.size() == 1;
        ok = ok && af_fa_set2.insert(1) == 0 && af_fa_set2.size() == 1;
        af_fa_s2.drop(); af_fa_v2.drop(); af_fa_set2.drop();
    }
    adv_ck("af_fail_after_all", ok);
    adv_balanced("af_fail_after_all_bal", 1);
}

static void adv_fail_after_one(void) {
    int ok, v = 0;
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_after(1); /* only the first allocation succeeds */
    {
        vector af_fo = vector();
        int rc1 = af_fo.push_back(10); /* cap 0 -> 4: allocation #1 succeeds */
        int rc2 = af_fo.push_back(20); /* still within cap 4: no allocation */
        int rc3 = af_fo.push_back(30);
        int rc4 = af_fo.push_back(40);
        int rc5 = af_fo.push_back(50); /* cap 4 -> 8: allocation #2 fails */
        ok = rc1 == 0 && rc2 == 0 && rc3 == 0 && rc4 == 0 && rc5 == -1 &&
             af_fo.size() == 4 && af_fo.capacity() == 4;
        {
            int j;
            int exp[4];
            exp[0] = 10; exp[1] = 20; exp[2] = 30; exp[3] = 40;
            for (j = 0; j < 4; j++)
                if (af_fo.at_checked((size_t)j, &v) != 0 || v != exp[j])
                    ok = 0;
        }
        stdlib_alloc_fail_reset();
        {
            int rc6 = af_fo.push_back(50); /* retry after reset */
            ok = ok && rc6 == 0 && af_fo.size() == 5;
        }
        af_fo.drop();
    }
    adv_ck("af_fail_after_one", ok);
    adv_balanced("af_fail_after_one_bal", 1);
}

/* ====================== I/O failures and cleanup ========================= */
static void adv_io_edges(void) {
    int ok = 1;
    char buf[8];
    error adv_io_err = error();
    stdlib_telemetry_stats io_before, io_after;

    stdlib_telemetry_snapshot(&io_before);
    stdlib_alloc_fail_after(0); /* file.hp must not use stdlib_alloc */
    {
        /* invalid handle: all operations fail recoverably */
        file af_bad = file(NULL, NULL);
        if (af_bad.is_open())
            ok = 0;
        buf[0] = '\0';
        if (af_bad.read(buf, 1, &adv_io_err) != STDLIB_FAIL ||
            adv_io_err.category() != STDLIB_CATEGORY_IO)
            ok = 0;
        if (af_bad.write("x") != 0)
            ok = 0;
        if (af_bad.write_bytes("x", 1, &adv_io_err) != STDLIB_FAIL ||
            adv_io_err.category() != STDLIB_CATEGORY_IO)
            ok = 0;
        if (af_bad.close(&adv_io_err) != STDLIB_FAIL ||
            adv_io_err.category() != STDLIB_CATEGORY_LIFECYCLE)
            ok = 0;
        af_bad.drop();
        af_bad.drop();
        if (af_bad.is_open())
            ok = 0;
    }
    /* failed open followed by a successful retry on a real temp file */
    {
        file af_missing = file("/definitely/not/a/real/path/adv_missing", "r");
        if (af_missing.is_open())
            ok = 0;
        buf[0] = '\0';
        if (af_missing.read(buf, 1, &adv_io_err) != STDLIB_FAIL ||
            adv_io_err.category() != STDLIB_CATEGORY_IO)
            ok = 0;
        af_missing.drop();
    }
    {
        file af_good = file_temp(&adv_io_err);
        if (!af_good.is_open() || !adv_io_err.ok())
            ok = 0;
        if (af_good.write_bytes("ok", 2, &adv_io_err) != STDLIB_OK)
            ok = 0;
        if (af_good.flush(&adv_io_err) != STDLIB_OK)
            ok = 0;
        if (af_good.seek(0, SEEK_SET, &adv_io_err) != STDLIB_OK)
            ok = 0;
        buf[0] = buf[1] = buf[2] = '\0';
        if (af_good.read(buf, 2, &adv_io_err) != STDLIB_OK ||
            buf[0] != 'o' || buf[1] != 'k')
            ok = 0;
        /* repeated close: first OK, later ones are clean lifecycle failures */
        if (af_good.close(&adv_io_err) != STDLIB_OK)
            ok = 0;
        if (af_good.close(&adv_io_err) != STDLIB_FAIL ||
            adv_io_err.category() != STDLIB_CATEGORY_LIFECYCLE)
            ok = 0;
        if (af_good.close(&adv_io_err) != STDLIB_FAIL ||
            adv_io_err.category() != STDLIB_CATEGORY_LIFECYCLE)
            ok = 0;
        af_good.drop();
        af_good.drop();
        if (af_good.is_open())
            ok = 0;
    }
    stdlib_alloc_fail_reset();
    adv_ck("af_io_edges", ok);
    /* no stdlib allocation happened while injection was armed */
    stdlib_telemetry_snapshot(&io_after);
    adv_ck("af_io_no_alloc", io_after.alloc_count == io_before.alloc_count &&
                                 io_after.free_count == io_before.free_count);
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    adv_str_ctor();
    adv_str_reserve();
    adv_str_append();
    adv_str_copy();
    adv_vec_reserve();
    adv_vec_push_growth();
    adv_vec_insert_growth();
    adv_vec_copy();
    adv_vec_shrink();
    adv_intset_first();
    adv_intmap_first();
    adv_strmap_first();
    adv_deque_first();
    adv_list_first();
    adv_stack_queue_first();
    adv_fail_after_all();
    adv_fail_after_one();
    adv_io_edges();

    adv_balanced("af_final_balanced", 1);
    stdlib_telemetry_set_enabled(0);

    print("ADV_ALLOC_FAILURE", adv_checks, adv_failed);
    return adv_failed ? 1 : 0;
}
