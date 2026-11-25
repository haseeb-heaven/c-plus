/* Adversarial regression family: container growth/shrinkage, collisions,
 * tombstone reuse, reserve boundaries and failure recovery.
 *
 * Scope (all seven containers in stdlib/collections.hp):
 *   intmap / intset / strmap  -- open-addressed hash tables:
 *        collision-heavy keys, tombstones, repeated rebuilds, grow()/clear(),
 *        injected allocation failure at every rebuild allocation point,
 *        key-copy failure on strmap, deterministic differential shadow model.
 *   deque / list / stack / queue -- ordered containers:
 *        fixed-seed LCG push/pop streams against a shadow value model,
 *        interleaved ends to force ring wrap-around, reserve boundaries,
 *        injected allocation failure when a growth is required.
 *
 * SELF-CHECKING: every check prints "<tag> <1|0>".  The final line is
 * "ADV_CONTAINERS <checks> <failed>".  Exit code 0 == all checks passed,
 * 1 == at least one check failed.  There is no external .expected fixture by
 * design: the tag stream plus the footer is the evidence.  All randomness is
 * a fixed-seed LCG, so every run is byte-for-byte reproducible.
 *
 * NOTE ON NAMING: the C+ compiler dispatches class methods by *variable name*
 * across the whole translation unit, so every container instance in this file
 * has a globally unique name (im/im_a/..., iset/..., sm/..., dq/..., ...).
 * Reusing a name with a different class makes the compiler pick the wrong
 * method overload; that is a toolchain limitation, not a library defect.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

static int g_checks = 0;
static int g_failed = 0;
static unsigned long long g_rng = 0ULL;

/* Collision key pools: int keys whose low hash bits are zero collide through
 * several table growths; str keys whose FNV low 6 bits are zero collide at
 * every capacity <= 64. */
static int g_ik[64];
static char g_sk[40][16];

/* Shared shadow model for the ordered containers.  g_seq holds the logical
 * element values in front-to-back order; g_seq_n is its length. */
static int g_seq[512];
static size_t g_seq_n;

static unsigned int rnd(void) {
    g_rng = g_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(g_rng >> 33);
}

static void ck(const char *tag, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(tag, cond ? 1 : 0);
}

static int count_present(const int *p, int n) {
    int c = 0;
    int i;
    for (i = 0; i < n; i++)
        c += p[i];
    return c;
}

static int collect_int_keys(unsigned long long mask, int need) {
    long long k = 0;
    int found = 0;
    while (found < need && k < 4000000LL) {
        if ((stdlib_map_hash_int(k) & mask) == 0ULL)
            g_ik[found++] = (int)k;
        k++;
    }
    return found;
}

static int collect_str_keys(int need) {
    int i;
    int found = 0;
    for (i = 0; found < need && i < 500000; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "sk%d", i);
        if ((stdlib_map_hash_text(buf) & 63ULL) == 0ULL) {
            snprintf(g_sk[found], 16, "%s", buf);
            found++;
        }
    }
    return found;
}

static void sh_reset(void) { g_seq_n = 0; }

static void sh_insert(size_t idx, int val) {
    size_t i;
    for (i = g_seq_n; i > idx; i--)
        g_seq[i] = g_seq[i - 1];
    g_seq[idx] = val;
    g_seq_n++;
}

static void sh_remove(size_t idx) {
    size_t i;
    for (i = idx; i + 1 < g_seq_n; i++)
        g_seq[i] = g_seq[i + 1];
    g_seq_n--;
}

/* ------------------------------------------------------------------ intmap */
static void test_intmap(void) {
    intmap im = intmap();
    int present[64];
    int val[64];
    int i, v = 0, got = 0, rc = 0, ok, bad, iter;
    int n = collect_int_keys(255ULL, 48);

    ck("ac_im_keys", n == 48);

    /* Deterministic differential stress with fixed seed over colliding keys. */
    for (i = 0; i < 48; i++)
        present[i] = 0;
    g_rng = 0x13579BDFULL;
    bad = 0;
    for (iter = 0; iter < 3000; iter++) {
        unsigned int r = rnd();
        int idx = (int)(r % 48U);
        int key = g_ik[idx];
        if (((r >> 8) % 3U) == 0U) {
            rc = im.remove(key);
            if (present[idx]) {
                if (rc != 0)
                    bad = 1;
                present[idx] = 0;
            } else if (rc == 0) {
                bad = 1;
            }
        } else {
            int nv = (int)((r >> 16) & 0xFFFFU);
            if (im.put(key, nv) != 0)
                bad = 1;
            present[idx] = 1;
            val[idx] = nv;
        }
    }
    ok = 1;
    for (i = 0; i < 48; i++) {
        int found = (im.get(g_ik[i], &got) == 0);
        if (found != present[i])
            ok = 0;
        else if (found && got != val[i])
            ok = 0;
    }
    ck("ac_im_diff", bad == 0 && ok &&
                         (int)im.size() == count_present(present, 48));

    /* Tombstone reuse: delete every key, then refill with fresh values. */
    for (i = 0; i < 48; i++)
        im.remove(g_ik[i]);
    ck("ac_im_tomb_empty", im.size() == 0);
    ok = 1;
    for (i = 0; i < 48; i++)
        if (im.put(g_ik[i], 5000 + i) != 0)
            ok = 0;
    ck("ac_im_tomb_reuse", ok && im.size() == 48);
    ok = 1;
    for (i = 0; i < 48; i++)
        if (im.get(g_ik[i], &got) != 0 || got != 5000 + i)
            ok = 0;
    ck("ac_im_tomb_values", ok);

    /* Reserve boundaries: no-op, real grow, overflow refusal, no shrink. */
    ck("ac_im_grow_noop", im.grow(8) == 0 && im.size() == 48);
    ck("ac_im_grow_real", im.grow(1024) == 0 && im.size() == 48 &&
                             im.get(g_ik[0], &got) == 0 && got == 5000);
    ck("ac_im_grow_overflow", im.grow(SIZE_MAX) == -1 && im.size() == 48 &&
                                  im.get(g_ik[47], &got) == 0 && got == 5047);
    im.clear();
    ck("ac_im_clear", im.size() == 0 && im.get(g_ik[0], &got) == -1);

    /* Failure recovery: rebuild needs three allocations.  Fail each one. */
    {
        intmap im_a = intmap();
        for (i = 0; i < 5; i++)
            im_a.put(i * 7, i * 10); /* cap 8, used_slots 5: next put rebuilds */
        for (v = 1; v <= 3; v++) {
            stdlib_alloc_fail_at(v);
            rc = im_a.put(999, 123);
            stdlib_alloc_fail_reset();
            ok = 1;
            for (i = 0; i < 5; i++) {
                got = 0;
                if (im_a.get(i * 7, &got) != 0 || got != i * 10)
                    ok = 0;
            }
            ck(v == 1 ? "ac_im_oom1" : (v == 2 ? "ac_im_oom2" : "ac_im_oom3"),
               rc == -1 && ok && im_a.size() == 5 && im_a.valid() == 1 &&
                   im_a.contains(999) == 0);
        }
        ck("ac_im_oom_retry",
           im_a.put(999, 123) == 0 && im_a.size() == 6 &&
               im_a.get(999, &got) == 0 && got == 123);
        im_a.drop();
    }
    /* fail_after(0): every allocation refused until reset. */
    {
        intmap im_b = intmap();
        for (i = 0; i < 5; i++)
            im_b.put(i, i);
        stdlib_alloc_fail_after(0);
        rc = im_b.put(999, 1);
        stdlib_alloc_fail_reset();
        ck("ac_im_fail_after", rc == -1 && im_b.size() == 5 &&
                                  im_b.valid() == 1 && im_b.contains(999) == 0);
        im_b.drop();
    }
    im.drop();
    im.drop();
    ck("ac_im_drop", im.size() == 0 && im.valid() == 0);
}

/* ------------------------------------------------------------------ intset */
static void test_intset(void) {
    intset iset = intset();
    int present[64];
    int i, got = 0, rc = 0, ok, bad, iter;
    int n = collect_int_keys(255ULL, 48);

    ck("ac_is_keys", n == 48);
    for (i = 0; i < 48; i++)
        present[i] = 0;
    g_rng = 0x2468ACE0ULL;
    bad = 0;
    for (iter = 0; iter < 3000; iter++) {
        unsigned int r = rnd();
        int idx = (int)(r % 48U);
        if (((r >> 8) % 3U) == 0U) {
            rc = iset.remove(g_ik[idx]);
            if (present[idx]) {
                if (rc != 0)
                    bad = 1;
                present[idx] = 0;
            } else if (rc == 0) {
                bad = 1;
            }
        } else {
            if (iset.insert(g_ik[idx]) != 0)
                bad = 1;
            present[idx] = 1;
        }
    }
    ok = 1;
    for (i = 0; i < 48; i++)
        if (iset.contains(g_ik[i]) != present[i])
            ok = 0;
    ck("ac_is_diff", bad == 0 && ok &&
                         (int)iset.size() == count_present(present, 48));

    for (i = 0; i < 48; i++)
        iset.remove(g_ik[i]);
    ck("ac_is_tomb_empty", iset.size() == 0);
    ok = 1;
    for (i = 0; i < 48; i++)
        if (iset.insert(g_ik[i]) != 0)
            ok = 0;
    ok = ok && iset.size() == 48;
    for (i = 0; i < 48; i++)
        if (!iset.contains(g_ik[i]))
            ok = 0;
    ck("ac_is_tomb_reuse", ok);
    ck("ac_is_grow", iset.grow(512) == 0 && iset.size() == 48 &&
                         iset.contains(g_ik[47]) == 1 &&
                         iset.grow(SIZE_MAX) == -1 && iset.size() == 48);

    /* Two rebuild buffers: fail each allocation point. */
    {
        intset iset_a = intset();
        for (i = 0; i < 5; i++)
            iset_a.insert(i);
        for (got = 1; got <= 2; got++) {
            stdlib_alloc_fail_at(got);
            rc = iset_a.insert(999);
            stdlib_alloc_fail_reset();
            ok = 1;
            for (i = 0; i < 5; i++)
                if (!iset_a.contains(i))
                    ok = 0;
            ck(got == 1 ? "ac_is_oom1" : "ac_is_oom2",
               rc == -1 && ok && iset_a.size() == 5 && iset_a.valid() == 1 &&
                   iset_a.contains(999) == 0);
        }
        ck("ac_is_oom_retry", iset_a.insert(999) == 0 &&
                                  iset_a.contains(999) == 1 &&
                                  iset_a.size() == 6);
        iset_a.drop();
    }
    iset.drop();
    iset.drop();
    ck("ac_is_drop", iset.size() == 0 && iset.valid() == 0);
}

/* ------------------------------------------------------------------ strmap */
static void test_strmap(void) {
    strmap sm = strmap();
    int present[40];
    int val[40];
    int i, got = 0, rc = 0, ok, bad, iter;
    int n = collect_str_keys(32);
    char key[16];

    ck("ac_sm_keys", n == 32);
    for (i = 0; i < 32; i++)
        present[i] = 0;
    g_rng = 0x0F0F0F0FULL;
    bad = 0;
    for (iter = 0; iter < 3000; iter++) {
        unsigned int r = rnd();
        int idx = (int)(r % 32U);
        if (((r >> 8) % 3U) == 0U) {
            rc = sm.remove(g_sk[idx]);
            if (present[idx]) {
                if (rc != 0)
                    bad = 1;
                present[idx] = 0;
            } else if (rc == 0) {
                bad = 1;
            }
        } else {
            int nv = (int)((r >> 16) & 0xFFFFU);
            if (sm.put(g_sk[idx], nv) != 0)
                bad = 1;
            present[idx] = 1;
            val[idx] = nv;
        }
    }
    ok = 1;
    for (i = 0; i < 32; i++) {
        int found = (sm.get(g_sk[i], &got) == 0);
        if (found != present[i])
            ok = 0;
        else if (found && got != val[i])
            ok = 0;
    }
    ck("ac_sm_diff", bad == 0 && ok &&
                         (int)sm.size() == count_present(present, 32));

    ck("ac_sm_null_put",
       sm.put(NULL, 5) == -1 && sm.size() == (size_t)count_present(present, 32));
    ck("ac_sm_null_query",
       sm.get(NULL, &got) == -1 && sm.contains(NULL) == 0 && sm.remove(NULL) == -1);
    ok = 1;
    for (i = 0; i < 32; i++)
        if (present[i] && (sm.get(g_sk[i], &got) != 0 || got != val[i]))
            ok = 0;
    ck("ac_sm_values_after_null", ok);

    /* clear() must release owned keys and leave a reusable, valid map. */
    sm.clear();
    ck("ac_sm_clear", sm.size() == 0 && sm.contains(g_sk[0]) == 0);
    ok = 1;
    for (i = 0; i < 32; i++)
        if (sm.put(g_sk[i], 7000 + i) != 0)
            ok = 0;
    ck("ac_sm_reuse", ok && sm.size() == 32 &&
                          sm.get(g_sk[31], &got) == 0 && got == 7031);

    /* Rebuild succeeds (3 buffers) but the 4th allocation (owned key copy)
     * fails: the put is refused with the map logically unchanged. */
    {
        strmap sm_a = strmap();
        for (i = 0; i < 5; i++) {
            snprintf(key, sizeof(key), "ok%d", i);
            sm_a.put(key, i);
        }
        stdlib_alloc_fail_at(4);
        rc = sm_a.put("fresh-key", 99);
        stdlib_alloc_fail_reset();
        ok = 1;
        for (i = 0; i < 5; i++) {
            snprintf(key, sizeof(key), "ok%d", i);
            if (sm_a.get(key, &got) != 0 || got != i)
                ok = 0;
        }
        ck("ac_sm_oom_keycopy", rc == -1 && ok && sm_a.size() == 5 &&
                                    sm_a.valid() == 1 &&
                                    sm_a.contains("fresh-key") == 0);
        ck("ac_sm_oom_retry",
           sm_a.put("fresh-key", 99) == 0 && sm_a.size() == 6 &&
               sm_a.get("fresh-key", &got) == 0 && got == 99);
        sm_a.drop();
    }

    sm.drop();
    sm.drop();
    ck("ac_sm_drop", sm.size() == 0 && sm.valid() == 0);
}

/* ----------------------------------------------------------------- deque */
static void test_deque(void) {
    deque dq = deque();
    int i, iter, got = 0, rc = 0, ok, bad;
    unsigned int r;

    ck("ac_dq_empty", dq.size() == 0 && dq.capacity() == 0 &&
                         dq.pop_front(&got) == -1 && dq.pop_back(&got) == -1 &&
                         dq.at_checked(0, &got) == -1 &&
                         dq.at_checked(0, NULL) == -1);

    sh_reset();
    g_rng = 0xA5A5A5A5ULL;
    bad = 0;
    for (iter = 0; iter < 4000; iter++) {
        r = rnd();
        if (g_seq_n < 8) {
            int v = (int)(r & 0xFFFFU);
            if ((r >> 4) & 1U) {
                if (dq.push_back(v) != 0) bad = 1;
                sh_insert(g_seq_n, v);
            } else {
                if (dq.push_front(v) != 0) bad = 1;
                sh_insert(0, v);
            }
        } else if (g_seq_n > 24) {
            if ((r >> 4) & 1U) {
                if (dq.pop_front(&got) != 0 || got != g_seq[0]) bad = 1;
                sh_remove(0);
            } else {
                int li = (int)g_seq_n - 1;
                if (dq.pop_back(&got) != 0 || got != g_seq[li]) bad = 1;
                sh_remove((size_t)li);
            }
        } else if (((r >> 4) & 3U) == 0U) {
            if (dq.pop_front(&got) != 0 || got != g_seq[0]) bad = 1;
            sh_remove(0);
        } else if (((r >> 4) & 3U) == 1U) {
            int li = (int)g_seq_n - 1;
            if (dq.pop_back(&got) != 0 || got != g_seq[li]) bad = 1;
            sh_remove((size_t)li);
        } else if (((r >> 4) & 1U) == 0U) {
            int v = (int)(r & 0xFFFFU);
            if (dq.push_back(v) != 0) bad = 1;
            sh_insert(g_seq_n, v);
        } else {
            int v = (int)(r & 0xFFFFU);
            if (dq.push_front(v) != 0) bad = 1;
            sh_insert(0, v);
        }
    }
    ok = 1;
    if (dq.size() != g_seq_n)
        ok = 0;
    for (i = 0; i < (int)g_seq_n; i++)
        if (dq.at_checked((size_t)i, &got) != 0 || got != g_seq[i])
            ok = 0;
    ck("ac_dq_diff", bad == 0 && ok);

    /* Reserve boundaries: reserve never shrinks; overflow is refused and the
     * contents survive. */
    ck("ac_dq_reserve_noop", dq.reserve(1) == 0 && dq.size() == g_seq_n);
    ck("ac_dq_reserve_grow", dq.reserve(256) == 0 && dq.capacity() >= 256 &&
                                 dq.size() == g_seq_n);
    ok = 1;
    for (i = 0; i < (int)g_seq_n; i++)
        if (dq.at_checked((size_t)i, &got) != 0 || got != g_seq[i])
            ok = 0;
    ck("ac_dq_reserve_values", ok);
    ck("ac_dq_reserve_overflow", dq.reserve(SIZE_MAX) == -1 &&
                                     dq.size() == g_seq_n &&
                                     dq.at_checked(0, &got) == 0);

    /* Failure recovery: fill to capacity, then inject failure of the single
     * ring-copy allocation that push_back requires. */
    {
        deque dq_a = deque();
        for (i = 0; i < 8; i++)
            dq_a.push_back(i);
        ck("ac_dq_full", dq_a.size() == 8 && dq_a.capacity() == 8);
        stdlib_alloc_fail_at(1);
        rc = dq_a.push_back(99);
        stdlib_alloc_fail_reset();
        ok = 1;
        for (i = 0; i < 8; i++)
            if (dq_a.at_checked((size_t)i, &got) != 0 || got != i)
                ok = 0;
        ck("ac_dq_oom", rc == -1 && ok && dq_a.size() == 8 &&
                            dq_a.capacity() == 8);
        ck("ac_dq_oom_retry",
           dq_a.push_back(99) == 0 && dq_a.size() == 9 &&
               dq_a.at_checked(8, &got) == 0 && got == 99);
        dq_a.drop();
    }

    dq.clear();
    ck("ac_dq_clear", dq.size() == 0 && dq.at_checked(0, &got) == -1);
    ck("ac_dq_push_after_clear",
       dq.push_back(7) == 0 && dq.at_checked(0, &got) == 0 && got == 7);
    dq.drop();
    ck("ac_dq_drop", dq.size() == 0 && dq.capacity() == 0);
}

/* ------------------------------------------------------------------- list */
static void test_list(void) {
    list lst = list();
    int i, iter, got = 0, rc = 0, ok, bad;
    unsigned int r;

    ck("ac_ls_empty", lst.size() == 0 && lst.pop_front(&got) == -1 &&
                         lst.pop_back(&got) == -1 && lst.at_checked(0, &got) == -1);

    sh_reset();
    g_rng = 0x5A5A5A5AULL;
    bad = 0;
    for (iter = 0; iter < 3000; iter++) {
        r = rnd();
        if (g_seq_n < 8) {
            int v = (int)(r & 0xFFFFU);
            if ((r >> 4) & 1U) {
                if (lst.push_back(v) != 0) bad = 1;
                sh_insert(g_seq_n, v);
            } else {
                if (lst.push_front(v) != 0) bad = 1;
                sh_insert(0, v);
            }
        } else if (g_seq_n > 20) {
            if ((r >> 4) & 1U) {
                if (lst.pop_front(&got) != 0 || got != g_seq[0]) bad = 1;
                sh_remove(0);
            } else {
                int li = (int)g_seq_n - 1;
                if (lst.pop_back(&got) != 0 || got != g_seq[li]) bad = 1;
                sh_remove((size_t)li);
            }
        } else if (((r >> 4) & 1U) == 0U) {
            if (lst.pop_front(&got) != 0 || got != g_seq[0]) bad = 1;
            sh_remove(0);
        } else {
            int v = (int)(r & 0xFFFFU);
            if (lst.push_back(v) != 0) bad = 1;
            sh_insert(g_seq_n, v);
        }
    }
    ok = 1;
    if (lst.size() != g_seq_n)
        ok = 0;
    for (i = 0; i < (int)g_seq_n; i++)
        if (lst.at_checked((size_t)i, &got) != 0 || got != g_seq[i])
            ok = 0;
    ck("ac_ls_diff", bad == 0 && ok);

    ck("ac_ls_oob", lst.at_checked(g_seq_n, &got) == -1 &&
                       lst.at_checked(0, NULL) == -1);
    /* Failure recovery: a single node allocation failing on push_back. */
    {
        list lst_a = list();
        lst_a.push_back(1);
        lst_a.push_back(2);
        stdlib_alloc_fail_at(1);
        rc = lst_a.push_back(3);
        stdlib_alloc_fail_reset();
        ck("ac_ls_oom", rc == -1 && lst_a.size() == 2 &&
                            lst_a.at_checked(0, &got) == 0 && got == 1 &&
                            lst_a.at_checked(1, &got) == 0 && got == 2);
        ck("ac_ls_oom_retry", lst_a.push_back(3) == 0 && lst_a.size() == 3 &&
                                  lst_a.at_checked(2, &got) == 0 && got == 3);
        lst_a.drop();
    }
    lst.clear();
    ck("ac_ls_clear", lst.size() == 0 && lst.at_checked(0, &got) == -1);
    lst.drop();
    lst.drop();
    ck("ac_ls_drop", lst.size() == 0);
}

/* ------------------------------------------------------------------ stack */
static void test_stack(void) {
    stack stk = stack();
    int i, iter, got = 0, rc = 0, ok, bad;
    unsigned int r;

    ck("ac_st_empty", stk.size() == 0 && stk.pop(&got) == -1 &&
                          stk.top_checked(&got) == -1 && stk.top_checked(NULL) == -1);

    sh_reset();
    g_rng = 0x33333333ULL;
    bad = 0;
    for (iter = 0; iter < 4000; iter++) {
        r = rnd();
        if (g_seq_n < 8 || (g_seq_n <= 24 && ((r >> 4) & 3U) != 0U)) {
            int v = (int)(r & 0xFFFFU);
            if (stk.push(v) != 0)
                bad = 1;
            sh_insert(g_seq_n, v);
        } else {
            int li = (int)g_seq_n - 1;
            if (stk.pop(&got) != 0 || got != g_seq[li])
                bad = 1;
            sh_remove((size_t)li);
        }
    }
    ok = 1;
    if (stk.size() != g_seq_n)
        ok = 0;
    if (g_seq_n > 0) {
        if (stk.top_checked(&got) != 0 || got != g_seq[g_seq_n - 1])
            ok = 0;
    } else if (stk.top_checked(&got) != -1) {
        ok = 0;
    }
    ck("ac_st_diff", bad == 0 && ok);

    /* Growth boundary: exactly 8 elements fill the first buffer. */
    {
        stack stk_a = stack();
        for (i = 0; i < 8; i++)
            stk_a.push(i);
        stdlib_alloc_fail_at(1);
        rc = stk_a.push(8); /* 9th push must reallocate 8 -> 16 */
        stdlib_alloc_fail_reset();
        ok = 1;
        for (i = 0; i < 8; i++)
            if (stk_a.pop(&got) != 0 || got != 7 - i)
                ok = 0;
        ck("ac_st_oom", rc == -1 && ok && stk_a.size() == 0);
        ck("ac_st_oom_retry", stk_a.push(50) == 0 &&
                                  stk_a.top_checked(&got) == 0 && got == 50);
        stk_a.drop();
    }
    stk.clear();
    ck("ac_st_clear", stk.size() == 0 && stk.top_checked(&got) == -1);
    ck("ac_st_reuse", stk.push(9) == 0 && stk.top_checked(&got) == 0 && got == 9);
    stk.drop();
    ck("ac_st_drop", stk.size() == 0);
}

/* ------------------------------------------------------------------ queue */
static void test_queue(void) {
    queue qu = queue();
    int i, iter, got = 0, rc = 0, ok, bad;
    unsigned int r;

    ck("ac_q_empty", qu.size() == 0 && qu.pop(&got) == -1 &&
                         qu.front_checked(&got) == -1 &&
                         qu.front_checked(NULL) == -1);

    sh_reset();
    g_rng = 0x77777777ULL;
    bad = 0;
    for (iter = 0; iter < 5000; iter++) {
        r = rnd();
        if (g_seq_n < 8) {
            int v = (int)(r & 0xFFFFU);
            if (qu.push(v) != 0)
                bad = 1;
            sh_insert(g_seq_n, v);
        } else if (g_seq_n > 24 || ((r >> 4) & 3U) == 0U) {
            if (qu.pop(&got) != 0 || got != g_seq[0])
                bad = 1;
            sh_remove(0);
        } else {
            int v = (int)(r & 0xFFFFU);
            if (qu.push(v) != 0)
                bad = 1;
            sh_insert(g_seq_n, v);
        }
    }
    ok = 1;
    if (qu.size() != g_seq_n)
        ok = 0;
    if (g_seq_n > 0) {
        if (qu.front_checked(&got) != 0 || got != g_seq[0])
            ok = 0;
    } else if (qu.front_checked(&got) != -1) {
        ok = 0;
    }
    ck("ac_q_diff", bad == 0 && ok);

    /* Wrap-around correctness: force head to move, then refill past the end
     * of the backing array so the ring wraps.  Shadow model is authoritative. */
    sh_reset();
    qu.clear(); /* discard LCG leftovers so the shadow starts from empty */
    for (i = 0; i < 8; i++) {
        qu.push(100 + i);
        sh_insert(g_seq_n, 100 + i);
    }
    for (i = 0; i < 5; i++) {
        if (qu.pop(&got) != 0 || got != g_seq[0]) bad = 1;
        sh_remove(0);
    }
    for (i = 0; i < 5; i++) {
        qu.push(200 + i);
        sh_insert(g_seq_n, 200 + i);
    }
    ok = 1;
    if (qu.size() != g_seq_n)
        ok = 0;
    {
        int total = (int)g_seq_n; /* sh_remove() shrinks g_seq_n as we pop */
        for (i = 0; i < total; i++) {
            if (qu.pop(&got) != 0 || got != g_seq[0])
                ok = 0;
            else
                sh_remove(0);
        }
    }
    ck("ac_q_wrap", bad == 0 && ok && qu.size() == 0);

    /* Failure recovery: full ring, injected failure of ring-copy on push. */
    {
        queue qu_a = queue();
        for (i = 0; i < 8; i++)
            qu_a.push(i);
        stdlib_alloc_fail_at(1);
        rc = qu_a.push(99);
        stdlib_alloc_fail_reset();
        ck("ac_q_oom", rc == -1 && qu_a.size() == 8 &&
                           qu_a.front_checked(&got) == 0 && got == 0);
        ok = 1;
        for (i = 0; i < 8; i++)
            if (qu_a.pop(&got) != 0 || got != i)
                ok = 0;
        ck("ac_q_oom_values", ok && qu_a.size() == 0);
        ck("ac_q_oom_retry", qu_a.push(99) == 0 && qu_a.size() == 1 &&
                                 qu_a.front_checked(&got) == 0 && got == 99);
        qu_a.drop();
    }
    qu.clear();
    ck("ac_q_clear", qu.size() == 0 && qu.front_checked(&got) == -1);
    ck("ac_q_reuse", qu.push(42) == 0 && qu.front_checked(&got) == 0 && got == 42);
    qu.drop();
    ck("ac_q_drop", qu.size() == 0);
}

/* ---------------------------------------------------------- alloc balance */
static void check_alloc_balance(void) {
    intmap bal_im = intmap();
    strmap bal_sm = strmap();
    deque bal_dq = deque();
    list bal_ls = list();
    stack bal_st = stack();
    queue bal_qu = queue();
    stdlib_telemetry_stats bal_tm;
    int i;

    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);
    for (i = 0; i < 64; i++) {
        bal_im.put(i, i * 3);
        bal_sm.put(g_sk[i % 32], i);
        bal_dq.push_back(i);
        bal_ls.push_back(i);
        bal_st.push(i);
        bal_qu.push(i);
    }
    /* Inject one failure on a hash-table growth, then recover. */
    stdlib_alloc_fail_at(1);
    (void)bal_im.grow(4096);
    stdlib_alloc_fail_reset();
    bal_im.drop();
    bal_sm.clear();
    bal_sm.drop();
    bal_dq.drop();
    bal_ls.drop();
    bal_st.drop();
    bal_qu.drop();
    stdlib_telemetry_snapshot(&bal_tm);
    ck("ac_leak_balance",
       bal_tm.alloc_count == bal_tm.free_count && bal_tm.alloc_count > 0 &&
           bal_tm.alloc_failures >= 1);
    stdlib_telemetry_set_enabled(0);
}

int main(void) {
    test_intmap();
    test_intset();
    test_strmap();
    test_deque();
    test_list();
    test_stack();
    test_queue();

    check_alloc_balance();

    print("ADV_CONTAINERS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
