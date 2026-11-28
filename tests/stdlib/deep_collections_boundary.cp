/* Deep contract tests for stdlib/collections.hp -- boundary values, invalid
 * arguments, overflow, lifecycle and deterministic fixed-seed stress.
 *
 * Covers all seven containers (intmap, intset, strmap, deque, list, stack,
 * queue):
 *   1. EMPTY containers: size()==0, accessors refuse, clear() no-op x3
 *   2. MINIMUM capacity: grow(0/1/7) -> cap 8; first push allocates 8
 *   3. MAXIMUM capacity: grow(STDLIB_ALLOC_LIMIT)/grow(SIZE_MAX) refuse
 *      recoverably with no allocation; deque::reserve(SIZE_MAX) refuses
 *   4. BOUNDARY growth: 5 keys in a cap-8 table, 6th triggers 8->16 rebuild;
 *      fill-to-8 + 9th push preserves wrapped ring order
 *   5. REPEATED insertion of the SAME key: size stays 1, last value wins,
 *      no allocation after the first
 *   6. REMOVAL of missing keys / NULL out-params: -1 with unchanged state
 *   7. TOMBSTONE REUSE: no table growth when 3 removed slots are reused
 *   8. LARGE DETERMINISTIC FIXED-SEED STRESS: 3072 ops per container against
 *      an independent shadow model, with a printed digest
 *   9. USE AFTER DROP: refused accessors, then zero-init semantics; double
 *      drop is inert
 *  10. INVALID ARGUMENTS: NULL keys/out-params and huge indices recover
 *
 * SELF-CHECKING: every check prints "<tag> 1" (or "<tag> 0") and increments a
 * failure counter.  The final records are "BOUNDARY <checks> <failed>" and a
 * "DIGEST <hi32> <lo32>" line (decimal).  main returns 0 only when no check
 * failed.  Exact stdout and commands live in
 * docs/stdlib/DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md.
 *
 * No header was modified to make a check pass.  The library's probe loops are
 * deliberately not instrumented; termination is proven by `timeout 30`.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

static int bd_checks = 0;
static int bd_failed = 0;
static unsigned long long bd_rng_state = 0ULL;
static unsigned long long bd_digest = 1469598103934665603ULL;

static void bd_check(const char *label, int cond) {
    bd_checks++;
    if (!cond)
        bd_failed++;
    print(label, cond ? 1 : 0);
}

/* Fixed-seed LCG: fully reproducible on every host and sanitizer mode. */
static unsigned int bd_rnd(void) {
    bd_rng_state = bd_rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(bd_rng_state >> 33);
}

static void bd_seed(unsigned long long bd_s) { bd_rng_state = bd_s; }

/* FNV-1a style fold of one observed value into the run digest. */
static void bd_mix(unsigned int bd_v) {
    bd_digest ^= (unsigned long long)bd_v;
    bd_digest *= 1099511628211ULL;
}

/* Shadow-model helpers for the linear (front->back) sequence containers. */
static void bd_sh_push_back(int *bd_a, int *bd_n, int bd_v) {
    bd_a[(*bd_n)++] = bd_v;
}
static void bd_sh_push_front(int *bd_a, int *bd_n, int bd_v) {
    int bd_j;
    for (bd_j = *bd_n; bd_j > 0; bd_j--)
        bd_a[bd_j] = bd_a[bd_j - 1];
    bd_a[0] = bd_v;
    (*bd_n)++;
}
static void bd_sh_pop_front(int *bd_a, int *bd_n) {
    int bd_j;
    for (bd_j = 1; bd_j < *bd_n; bd_j++)
        bd_a[bd_j - 1] = bd_a[bd_j];
    (*bd_n)--;
}
static void bd_sh_pop_back(int *bd_a, int *bd_n) { (*bd_n)--; }
/* Guarded back read: keeps the safe-bounds checker able to prove n >= 1. */
static int bd_sh_back(int *bd_a, int bd_n) {
    if (bd_n <= 0)
        return 0;
    return bd_a[bd_n - 1];
}

int main(void) {
    int bd_i;

    /* ---- 1. EMPTY CONTAINERS --------------------------------------------- */
    {
        intmap em = intmap();
        intset es = intset();
        strmap et = strmap();
        deque ed = deque();
        list el = list();
        stack ek = stack();
        queue eq = queue();
        int eo = -7;
        int eok;

        eok = em.size() == 0 && em.valid() == 0 && em.get(1, &eo) == -1 &&
              em.contains(1) == 0 && em.remove(1) == -1 && eo == -7;
        em.clear(); em.clear(); em.clear();
        bd_check("bd_empty_intmap", eok && em.size() == 0 && eo == -7);

        eo = -7;
        eok = es.size() == 0 && es.valid() == 0 && es.contains(1) == 0 &&
              es.remove(1) == -1 && eo == -7;
        es.clear(); es.clear(); es.clear();
        bd_check("bd_empty_intset", eok && es.size() == 0 && eo == -7);

        eo = -7;
        eok = et.size() == 0 && et.valid() == 0 && et.get("x", &eo) == -1 &&
              et.contains("x") == 0 && et.remove("x") == -1 && eo == -7;
        et.clear(); et.clear(); et.clear();
        bd_check("bd_empty_strmap", eok && et.size() == 0 && eo == -7);

        eo = -7;
        eok = ed.size() == 0 && ed.capacity() == 0 &&
              ed.pop_front(&eo) == -1 && ed.pop_back(&eo) == -1 &&
              ed.at_checked(0, &eo) == -1 && eo == -7;
        ed.clear(); ed.clear(); ed.clear();
        bd_check("bd_empty_deque", eok && ed.size() == 0 && eo == -7);

        eo = -7;
        eok = el.size() == 0 && el.pop_front(&eo) == -1 &&
              el.pop_back(&eo) == -1 && el.at_checked(0, &eo) == -1 && eo == -7;
        el.clear(); el.clear(); el.clear();
        bd_check("bd_empty_list", eok && el.size() == 0 && eo == -7);

        eo = -7;
        eok = ek.size() == 0 && ek.pop(&eo) == -1 &&
              ek.top_checked(&eo) == -1 && eo == -7;
        ek.clear(); ek.clear(); ek.clear();
        bd_check("bd_empty_stack", eok && ek.size() == 0 && eo == -7);

        eo = -7;
        eok = eq.size() == 0 && eq.pop(&eo) == -1 &&
              eq.front_checked(&eo) == -1 && eo == -7;
        eq.clear(); eq.clear(); eq.clear();
        bd_check("bd_empty_queue", eok && eq.size() == 0 && eo == -7);
    }

    /* ---- 2. MINIMUM CAPACITY --------------------------------------------- */
    {
        intmap cm = intmap();
        intset cs = intset();
        strmap ct = strmap();
        deque cd = deque();
        list cl = list();
        stack ck = stack();
        queue cq = queue();
        int co = 0;
        long cseq;

        bd_check("bd_mincap_intmap",
                 cm.valid() == 0 && cm.grow(0) == 0 && cm.valid() == 1 &&
                 cm.grow(1) == 0 && cm.grow(7) == 0 && cm.size() == 0 &&
                 cm.put(10, 20) == 0 && cm.size() == 1 &&
                 cm.get(10, &co) == 0 && co == 20);

        bd_check("bd_mincap_intset",
                 cs.valid() == 0 && cs.grow(0) == 0 && cs.valid() == 1 &&
                 cs.grow(1) == 0 && cs.grow(7) == 0 && cs.size() == 0 &&
                 cs.insert(11) == 0 && cs.size() == 1 && cs.contains(11));

        bd_check("bd_mincap_strmap",
                 ct.valid() == 0 && ct.grow(0) == 0 && ct.valid() == 1 &&
                 ct.grow(1) == 0 && ct.grow(7) == 0 && ct.size() == 0 &&
                 ct.put("mc", 21) == 0 && ct.size() == 1 &&
                 ct.get("mc", &co) == 0 && co == 21);

        stdlib_alloc_fail_reset(); cseq = stdlib_alloc_seq();
        bd_check("bd_mincap_deque",
                 cd.push_back(12) == 0 && cd.size() == 1 &&
                 cd.capacity() == 8 && (stdlib_alloc_seq() - cseq) == 1 &&
                 cd.at_checked(0, &co) == 0 && co == 12);

        stdlib_alloc_fail_reset(); cseq = stdlib_alloc_seq();
        bd_check("bd_mincap_list",
                 cl.push_back(13) == 0 && cl.size() == 1 &&
                 (stdlib_alloc_seq() - cseq) == 1 &&
                 cl.at_checked(0, &co) == 0 && co == 13);

        stdlib_alloc_fail_reset(); cseq = stdlib_alloc_seq();
        bd_check("bd_mincap_stack",
                 ck.push(14) == 0 && ck.size() == 1 &&
                 (stdlib_alloc_seq() - cseq) == 1 &&
                 ck.top_checked(&co) == 0 && co == 14);

        stdlib_alloc_fail_reset(); cseq = stdlib_alloc_seq();
        bd_check("bd_mincap_queue",
                 cq.push(15) == 0 && cq.size() == 1 &&
                 (stdlib_alloc_seq() - cseq) == 1 &&
                 cq.front_checked(&co) == 0 && co == 15);
    }

    /* ---- 3. MAXIMUM CAPACITY / OVERFLOW REFUSALS -------------------------- */
    {
        intmap xm = intmap();
        intset xs = intset();
        strmap xt = strmap();
        deque xd = deque();
        long xseq, xdelta;
        int xo = -999, xr1, xr2;

        xm.put(1, 11); xm.put(2, 22);
        stdlib_alloc_fail_reset(); xseq = stdlib_alloc_seq();
        xr1 = xm.grow(STDLIB_ALLOC_LIMIT);
        xr2 = xm.grow(SIZE_MAX);
        xdelta = stdlib_alloc_seq() - xseq;
        bd_check("bd_maxcap_intmap",
                 xr1 == -1 && xr2 == -1 && xdelta == 0 &&
                 xm.size() == 2 && xm.get(1, &xo) == 0 && xo == 11 &&
                 xm.get(2, &xo) == 0 && xo == 22);

        xs.insert(1); xs.insert(2);
        stdlib_alloc_fail_reset(); xseq = stdlib_alloc_seq();
        xr1 = xs.grow(STDLIB_ALLOC_LIMIT);
        xr2 = xs.grow(SIZE_MAX);
        xdelta = stdlib_alloc_seq() - xseq;
        bd_check("bd_maxcap_intset",
                 xr1 == -1 && xr2 == -1 && xdelta == 0 &&
                 xs.size() == 2 && xs.contains(1) && xs.contains(2));

        xt.put("a", 11); xt.put("b", 22);
        stdlib_alloc_fail_reset(); xseq = stdlib_alloc_seq();
        xr1 = xt.grow(STDLIB_ALLOC_LIMIT);
        xr2 = xt.grow(SIZE_MAX);
        xdelta = stdlib_alloc_seq() - xseq;
        bd_check("bd_maxcap_strmap",
                 xr1 == -1 && xr2 == -1 && xdelta == 0 &&
                 xt.size() == 2 && xt.get("a", &xo) == 0 && xo == 11 &&
                 xt.get("b", &xo) == 0 && xo == 22);

        xd.push_back(77);
        stdlib_alloc_fail_reset(); xseq = stdlib_alloc_seq();
        xr1 = xd.reserve(SIZE_MAX);
        xdelta = stdlib_alloc_seq() - xseq;
        bd_check("bd_maxcap_deque_reserve",
                 xr1 == -1 && xdelta == 0 && xd.size() == 1 &&
                 xd.capacity() == 8 && xd.at_checked(0, &xo) == 0 && xo == 77);
    }

    /* ---- 4. BOUNDARY GROWTH (cap 8 -> 16, wrapped rings) ------------------ */
    {
        intmap bm = intmap();
        intset bs = intset();
        strmap bt = strmap();
        deque bd = deque();
        deque bdw = deque();
        list bl = list();
        stack bk = stack();
        queue bq = queue();
        int bo = -999, bok, bdelta;
        long bseq;
        char bkey[16];

        for (bd_i = 0; bd_i < 5; bd_i++)
            bm.put(bd_i, 100 + bd_i);
        stdlib_alloc_fail_reset(); bseq = stdlib_alloc_seq();
        bok = bm.put(5, 105) == 0;
        bdelta = (int)(stdlib_alloc_seq() - bseq); /* 3 rebuild buffers */
        for (bd_i = 0; bd_i < 6; bd_i++) {
            if (bm.get(bd_i, &bo) != 0 || bo != 100 + bd_i)
                bok = 0;
        }
        bd_check("bd_bound_intmap",
                 bok && bm.size() == 6 && bdelta == 3);

        for (bd_i = 0; bd_i < 5; bd_i++)
            bs.insert(bd_i);
        stdlib_alloc_fail_reset(); bseq = stdlib_alloc_seq();
        bok = bs.insert(5) == 0;
        bdelta = (int)(stdlib_alloc_seq() - bseq); /* 2 rebuild buffers */
        for (bd_i = 0; bd_i < 6; bd_i++)
            if (!bs.contains(bd_i)) bok = 0;
        bd_check("bd_bound_intset",
                 bok && bs.size() == 6 && bdelta == 2);

        for (bd_i = 0; bd_i < 5; bd_i++) {
            snprintf(bkey, sizeof(bkey), "b%d", bd_i);
            bt.put(bkey, 100 + bd_i);
        }
        stdlib_alloc_fail_reset(); bseq = stdlib_alloc_seq();
        bok = bt.put("b5", 105) == 0;
        bdelta = (int)(stdlib_alloc_seq() - bseq); /* 3 buffers + key copy */
        for (bd_i = 0; bd_i < 6; bd_i++) {
            snprintf(bkey, sizeof(bkey), "b%d", bd_i);
            if (bt.get(bkey, &bo) != 0 || bo != 100 + bd_i)
                bok = 0;
        }
        bd_check("bd_bound_strmap",
                 bok && bt.size() == 6 && bdelta == 4);

        for (bd_i = 0; bd_i < 8; bd_i++)
            bd.push_back(bd_i);
        stdlib_alloc_fail_reset(); bseq = stdlib_alloc_seq();
        bok = bd.push_back(8) == 0;
        bdelta = (int)(stdlib_alloc_seq() - bseq);
        for (bd_i = 0; bd_i < 9; bd_i++) {
            if (bd.at_checked((size_t)bd_i, &bo) != 0 || bo != bd_i)
                bok = 0;
        }
        bd_check("bd_bound_deque",
                 bok && bd.size() == 9 && bd.capacity() == 16 && bdelta == 1);

        for (bd_i = 0; bd_i < 8; bd_i++)
            bl.push_back(bd_i);
        bok = bl.push_back(8) == 0;
        for (bd_i = 0; bd_i < 9; bd_i++) {
            if (bl.at_checked((size_t)bd_i, &bo) != 0 || bo != bd_i)
                bok = 0;
        }
        bd_check("bd_bound_list", bok && bl.size() == 9);

        for (bd_i = 0; bd_i < 8; bd_i++)
            bk.push(bd_i);
        bok = bk.push(8) == 0;
        for (bd_i = 8; bd_i >= 0; bd_i--) {
            if (bk.pop(&bo) != 0 || bo != bd_i)
                bok = 0;
        }
        bd_check("bd_bound_stack", bok && bk.size() == 0);

        for (bd_i = 0; bd_i < 8; bd_i++)
            bq.push(bd_i);
        bok = bq.push(8) == 0;
        for (bd_i = 0; bd_i < 9; bd_i++) {
            if (bq.pop(&bo) != 0 || bo != bd_i)
                bok = 0;
        }
        bd_check("bd_bound_queue", bok && bq.size() == 0);

        /* Wrapped ring: head moved off zero before the growth rebuild. */
        bdw.push_front(100); bdw.push_front(101); bdw.push_front(102);
        bdw.push_back(103); bdw.push_back(104); bdw.push_back(105);
        bdw.push_back(106); bdw.push_back(107); bdw.push_back(108);
        bok = bdw.size() == 9;
        {
            int bwant[9];
            bwant[0] = 102; bwant[1] = 101; bwant[2] = 100;
            bwant[3] = 103; bwant[4] = 104; bwant[5] = 105;
            bwant[6] = 106; bwant[7] = 107; bwant[8] = 108;
            for (bd_i = 0; bd_i < 9; bd_i++)
                if (bdw.at_checked((size_t)bd_i, &bo) != 0 || bo != bwant[bd_i])
                    bok = 0;
        }
        bd_check("bd_bound_deque_wrap", bok && bdw.capacity() == 16);

        /* Queue wraps naturally: pop three, refill, then push the 9th. */
        for (bd_i = 1; bd_i <= 8; bd_i++)
            bq.push(bd_i);
        for (bd_i = 0; bd_i < 3; bd_i++)
            bq.pop(&bo);
        for (bd_i = 9; bd_i <= 11; bd_i++)
            bq.push(bd_i);
        bq.push(12);
        bok = bq.size() == 9;
        for (bd_i = 4; bd_i <= 12; bd_i++) {
            if (bq.pop(&bo) != 0 || bo != bd_i)
                bok = 0;
        }
        bd_check("bd_bound_queue_wrap", bok && bq.size() == 0);
    }

    /* ---- 5. REPEATED INSERTION OF THE SAME KEY ---------------------------- */
    {
        intmap rm = intmap();
        intset rs = intset();
        strmap rt = strmap();
        int ro = -999, rok;
        long rseq;

        rseq = 0;
        for (bd_i = 0; bd_i < 50; bd_i++) {
            rm.put(77, bd_i);
            if (bd_i == 0) {
                stdlib_alloc_fail_reset();
                rseq = stdlib_alloc_seq();
            }
        }
        rok = rm.size() == 1 && (stdlib_alloc_seq() - rseq) == 0 &&
              rm.get(77, &ro) == 0 && ro == 49 && !rm.contains(78);
        bd_check("bd_repeat_intmap", rok);

        rseq = 0;
        for (bd_i = 0; bd_i < 50; bd_i++) {
            rs.insert(77);
            if (bd_i == 0) {
                stdlib_alloc_fail_reset();
                rseq = stdlib_alloc_seq();
            }
        }
        rok = rs.size() == 1 && (stdlib_alloc_seq() - rseq) == 0 &&
              rs.contains(77) && !rs.contains(78);
        bd_check("bd_repeat_intset", rok);

        rseq = 0;
        for (bd_i = 0; bd_i < 50; bd_i++) {
            rt.put("samekey", bd_i);
            if (bd_i == 0) {
                stdlib_alloc_fail_reset();
                rseq = stdlib_alloc_seq();
            }
        }
        rok = rt.size() == 1 && (stdlib_alloc_seq() - rseq) == 0 &&
              rt.get("samekey", &ro) == 0 && ro == 49;
        bd_check("bd_repeat_strmap", rok);
    }

    /* ---- 6. REMOVAL OF MISSING KEYS / NULL OUT-PARAMS --------------------- */
    {
        intmap zm = intmap();
        intset zs = intset();
        strmap zt = strmap();
        deque zd = deque();
        list zl = list();
        stack zk = stack();
        queue zq = queue();
        int zo = 99, zok;

        zok = zm.remove(5) == -1;          /* empty map */
        zm.put(1, 1); zm.put(2, 2);
        zok = zok && zm.remove(999) == -1 && zm.size() == 2 &&
              zm.get(999, &zo) == -1 && zo == 99;

        zok = zok && zs.remove(5) == -1;   /* empty set */
        zs.insert(1); zs.insert(2);
        zok = zok && zs.remove(999) == -1 && zs.size() == 2 &&
              !zs.contains(999);

        zok = zok && zt.remove("z") == -1; /* empty strmap */
        zt.put("a", 1); zt.put("b", 2);
        zok = zok && zt.remove("zz") == -1 && zt.size() == 2 &&
              zt.get("zz", &zo) == -1 && zo == 99;

        zok = zok && zm.get(1, NULL) == -1 && zt.get("a", NULL) == -1;
        bd_check("bd_missing_maps", zok && zm.size() == 2 && zt.size() == 2);

        zd.push_back(1);
        zo = 99;
        zok = zd.pop_front(NULL) == -1 && zd.pop_back(NULL) == -1 &&
              zd.at_checked(0, NULL) == -1 &&
              zd.at_checked(999, &zo) == -1 && zo == 99 && zd.size() == 1;
        bd_check("bd_nullout_deque", zok);

        zl.push_back(1);
        zo = 99;
        zok = zl.pop_front(NULL) == -1 && zl.pop_back(NULL) == -1 &&
              zl.at_checked(0, NULL) == -1 &&
              zl.at_checked(999, &zo) == -1 && zo == 99 && zl.size() == 1;
        bd_check("bd_nullout_list", zok);

        zk.push(1);
        zo = 99;
        zok = zk.pop(NULL) == -1 && zk.top_checked(NULL) == -1 &&
              zo == 99 && zk.size() == 1;
        bd_check("bd_nullout_stack", zok);

        zq.push(1);
        zo = 99;
        zok = zq.pop(NULL) == -1 && zq.front_checked(NULL) == -1 &&
              zo == 99 && zq.size() == 1;
        bd_check("bd_nullout_queue", zok);
    }

    /* ---- 7. TOMBSTONE REUSE (no table growth) ---------------------------- */
    {
        intmap tm = intmap();
        intset ts = intset();
        strmap tt = strmap();
        stdlib_telemetry_stats tg0, tg1;
        char tkey[16];
        int to = -999, tok;
        long tseq, tdelta;

        /* cap 16 leaves headroom so 5 used + 3 tombstones + 3 reuse = 5 used. */
        tm.grow(16);
        for (bd_i = 0; bd_i < 5; bd_i++)
            tm.put(bd_i, 100 + bd_i);
        tm.remove(0); tm.remove(2); tm.remove(4);
        stdlib_telemetry_set_enabled(1);
        stdlib_telemetry_reset();
        stdlib_telemetry_snapshot(&tg0);
        stdlib_alloc_fail_reset(); tseq = stdlib_alloc_seq();
        for (bd_i = 0; bd_i < 3; bd_i++)
            tm.put(50 + bd_i, 500 + bd_i);
        tdelta = stdlib_alloc_seq() - tseq;
        stdlib_telemetry_snapshot(&tg1);
        tok = tg1.container_growths == tg0.container_growths && tdelta == 0 &&
              tm.size() == 5 && !tm.contains(0) && !tm.contains(2) &&
              !tm.contains(4) && tm.get(1, &to) == 0 && to == 101 &&
              tm.get(3, &to) == 0 && to == 103 &&
              tm.get(50, &to) == 0 && to == 500 &&
              tm.get(51, &to) == 0 && to == 501 &&
              tm.get(52, &to) == 0 && to == 502;
        bd_check("bd_tomb_intmap", tok);

        ts.grow(16);
        for (bd_i = 0; bd_i < 5; bd_i++)
            ts.insert(bd_i);
        ts.remove(0); ts.remove(2); ts.remove(4);
        stdlib_telemetry_reset();
        stdlib_telemetry_snapshot(&tg0);
        stdlib_alloc_fail_reset(); tseq = stdlib_alloc_seq();
        for (bd_i = 0; bd_i < 3; bd_i++)
            ts.insert(50 + bd_i);
        tdelta = stdlib_alloc_seq() - tseq;
        stdlib_telemetry_snapshot(&tg1);
        tok = tg1.container_growths == tg0.container_growths && tdelta == 0 &&
              ts.size() == 5 && !ts.contains(0) && !ts.contains(2) &&
              !ts.contains(4) && ts.contains(1) && ts.contains(3) &&
              ts.contains(50) && ts.contains(51) && ts.contains(52);
        bd_check("bd_tomb_intset", tok);

        tt.grow(16);
        for (bd_i = 0; bd_i < 5; bd_i++) {
            snprintf(tkey, sizeof(tkey), "t%d", bd_i);
            tt.put(tkey, 100 + bd_i);
        }
        tt.remove("t0"); tt.remove("t2"); tt.remove("t4");
        stdlib_telemetry_reset();
        stdlib_telemetry_snapshot(&tg0);
        for (bd_i = 0; bd_i < 3; bd_i++) {
            snprintf(tkey, sizeof(tkey), "n%d", bd_i);
            tt.put(tkey, 500 + bd_i);
        }
        stdlib_telemetry_snapshot(&tg1);
        /* strmap new keys copy their string (3 small allocs) but never grow. */
        tok = tg1.container_growths == tg0.container_growths &&
              tt.size() == 5 && !tt.contains("t0") && !tt.contains("t2") &&
              !tt.contains("t4") && tt.get("t1", &to) == 0 && to == 101 &&
              tt.get("t3", &to) == 0 && to == 103 &&
              tt.get("n0", &to) == 0 && to == 500 &&
              tt.get("n1", &to) == 0 && to == 501 &&
              tt.get("n2", &to) == 0 && to == 502;
        bd_check("bd_tomb_strmap", tok);
        stdlib_telemetry_set_enabled(0);
    }

    /* ---- 8. LARGE DETERMINISTIC FIXED-SEED STRESS ------------------------- */
    {
        /* deque: push_front/back + pop_front/back vs front->back shadow */
        deque sd = deque();
        int ssh[64], sn = 0, sok = 1, sit, sop, sv, si;
        unsigned int sr;
        int sg1 = -1, sg2 = -1;
        bd_seed(0xC0FFEEULL);
        for (sit = 0; sit < 3072; sit++) {
            sr = bd_rnd();
            sop = (int)(sr % 4U);
            sv = (int)((sr >> 8) & 0xFFFFU);
            if (sop == 0) {
                if (sn < 24) {
                    if (sd.push_back(sv)) sok = 0;
                    bd_sh_push_back(ssh, &sn, sv);
                }
            } else if (sop == 1) {
                if (sn < 24) {
                    if (sd.push_front(sv)) sok = 0;
                    bd_sh_push_front(ssh, &sn, sv);
                }
            } else if (sop == 2) {
                if (sn > 0) {
                    if (sd.pop_front(&sg1) != 0 || sg1 != ssh[0]) sok = 0;
                    bd_sh_pop_front(ssh, &sn);
                }
            } else {
                if (sn > 0) {
                    if (sd.pop_back(&sg1) != 0 || sg1 != bd_sh_back(ssh, sn)) sok = 0;
                    bd_sh_pop_back(ssh, &sn);
                }
            }
            if ((int)sd.size() != sn) sok = 0;
            for (si = 0; si < sn; si++) {
                sg1 = -12345;
                if (sd.at_checked((size_t)si, &sg1) != 0 || sg1 != ssh[si])
                    sok = 0;
            }
            bd_mix((unsigned int)sn);
            if (sn > 0) {
                sg1 = -1; sg2 = -1;
                sd.at_checked(0, &sg1);
                sd.at_checked((size_t)(sn - 1), &sg2);
                bd_mix((unsigned int)sg1);
                bd_mix((unsigned int)sg2);
            }
        }
        bd_check("bd_stress_deque", sok && sn >= 0);
        sd.drop();

        /* list: same four operations, O(n) positional reads */
        {
            list sl = list();
            int lsh[64], ln = 0, lok = 1, lit, lop, lv, li;
            unsigned int lr;
            int lg1 = -1, lg2 = -1;
            bd_seed(0xC0FFEEULL);
            for (lit = 0; lit < 3072; lit++) {
                lr = bd_rnd();
                lop = (int)(lr % 4U);
                lv = (int)((lr >> 8) & 0xFFFFU);
                if (lop == 0) {
                    if (ln < 24) {
                        if (sl.push_back(lv)) lok = 0;
                        bd_sh_push_back(lsh, &ln, lv);
                    }
                } else if (lop == 1) {
                    if (ln < 24) {
                        if (sl.push_front(lv)) lok = 0;
                        bd_sh_push_front(lsh, &ln, lv);
                    }
                } else if (lop == 2) {
                    if (ln > 0) {
                        if (sl.pop_front(&lg1) != 0 || lg1 != lsh[0]) lok = 0;
                        bd_sh_pop_front(lsh, &ln);
                    }
                } else {
                    if (ln > 0) {
                        if (sl.pop_back(&lg1) != 0 || lg1 != bd_sh_back(lsh, ln)) lok = 0;
                        bd_sh_pop_back(lsh, &ln);
                    }
                }
                if ((int)sl.size() != ln) lok = 0;
                for (li = 0; li < ln; li++) {
                    lg1 = -12345;
                    if (sl.at_checked((size_t)li, &lg1) != 0 || lg1 != lsh[li])
                        lok = 0;
                }
                bd_mix((unsigned int)ln);
                if (ln > 0) {
                    lg1 = -1; lg2 = -1;
                    sl.at_checked(0, &lg1);
                    sl.at_checked((size_t)(ln - 1), &lg2);
                    bd_mix((unsigned int)lg1);
                    bd_mix((unsigned int)lg2);
                }
            }
            bd_check("bd_stress_list", lok && ln >= 0);
            sl.drop();
        }

        /* stack: push/pop/observe top vs LIFO shadow */
        {
            stack ss = stack();
            int ksh[64], kn = 0, kok = 1, kit, kop, kv, ki;
            unsigned int kr;
            int kg = -1;
            bd_seed(0xC0FFEEULL);
            for (kit = 0; kit < 3072; kit++) {
                kr = bd_rnd();
                kop = (int)(kr % 3U);
                kv = (int)((kr >> 8) & 0xFFFFU);
                if (kop == 0 && kn < 24) {
                    if (ss.push(kv)) kok = 0;
                    ksh[kn++] = kv;
                } else if (kop == 1 && kn > 0) {
                    if (ss.pop(&kg) != 0 || kg != bd_sh_back(ksh, kn)) kok = 0;
                    kn--;
                }
                if ((int)ss.size() != kn) kok = 0;
                if (kn > 0) {
                    kg = -1;
                    if (ss.top_checked(&kg) != 0 || kg != bd_sh_back(ksh, kn)) kok = 0;
                    bd_mix((unsigned int)kg);
                }
                bd_mix((unsigned int)kn);
                for (ki = 0; ki < kn; ki++)
                    bd_mix((unsigned int)ksh[ki]);
            }
            bd_check("bd_stress_stack", kok && kn >= 0);
            ss.drop();
        }

        /* queue: push/pop/observe front vs FIFO shadow */
        {
            queue sq = queue();
            int qsh[64], qn = 0, qok = 1, qit, qop, qv;
            unsigned int qr;
            int qg = -1;
            bd_seed(0xC0FFEEULL);
            for (qit = 0; qit < 3072; qit++) {
                qr = bd_rnd();
                qop = (int)(qr % 3U);
                qv = (int)((qr >> 8) & 0xFFFFU);
                if (qop == 0 && qn < 24) {
                    if (sq.push(qv)) qok = 0;
                    qsh[qn++] = qv;
                } else if (qop == 1 && qn > 0) {
                    if (sq.pop(&qg) != 0 || qg != qsh[0]) qok = 0;
                    bd_sh_pop_front(qsh, &qn);
                }
                if ((int)sq.size() != qn) qok = 0;
                if (qn > 0) {
                    qg = -1;
                    if (sq.front_checked(&qg) != 0 || qg != qsh[0]) qok = 0;
                    bd_mix((unsigned int)qg);
                }
                bd_mix((unsigned int)qn);
            }
            bd_check("bd_stress_queue", qok && qn >= 0);
            sq.drop();
        }

        /* intmap: 128-key shadow, full-scan verification after every op */
        {
            intmap sm = intmap();
            int mpresent[128], mval[128], mcount = 0, mok = 1, mit, mkey, mv;
            unsigned int mr;
            int mrc, mgot, mfound, mi;
            for (mi = 0; mi < 128; mi++)
                mpresent[mi] = 0;
            bd_seed(0xC0FFEEULL);
            for (mit = 0; mit < 3072; mit++) {
                mr = bd_rnd();
                mkey = (int)(mr % 128U);
                mv = (int)((mr >> 8) & 0xFFFFU);
                if (((mr >> 20) & 1U) == 0U) {
                    if (sm.put(mkey, mv) != 0) mok = 0;
                    if (!mpresent[mkey]) { mpresent[mkey] = 1; mcount++; }
                    mval[mkey] = mv;
                } else {
                    mrc = sm.remove(mkey);
                    if (mpresent[mkey]) {
                        if (mrc != 0) mok = 0;
                        mpresent[mkey] = 0; mcount--;
                    } else if (mrc == 0) {
                        mok = 0;
                    }
                }
                if ((int)sm.size() != mcount) mok = 0;
                for (mi = 0; mi < 128; mi++) {
                    mgot = -1;
                    mfound = (sm.get(mi, &mgot) == 0);
                    if (mfound != mpresent[mi]) mok = 0;
                    else if (mfound && mgot != mval[mi]) mok = 0;
                }
                bd_mix((unsigned int)mcount);
                bd_mix((unsigned int)mpresent[mkey]);
            }
            bd_check("bd_stress_intmap", mok && mcount >= 0);
            sm.drop();
        }

        /* intset: 128-key shadow, full-scan verification after every op */
        {
            intset ss2 = intset();
            int spresent[128], scount = 0, sok2 = 1, sit2, skey2;
            unsigned int sr2;
            int src, sfound, si2;
            for (si2 = 0; si2 < 128; si2++)
                spresent[si2] = 0;
            bd_seed(0xC0FFEEULL);
            for (sit2 = 0; sit2 < 3072; sit2++) {
                sr2 = bd_rnd();
                skey2 = (int)(sr2 % 128U);
                if (((sr2 >> 20) & 1U) == 0U) {
                    if (ss2.insert(skey2) != 0) sok2 = 0;
                    if (!spresent[skey2]) { spresent[skey2] = 1; scount++; }
                } else {
                    src = ss2.remove(skey2);
                    if (spresent[skey2]) {
                        if (src != 0) sok2 = 0;
                        spresent[skey2] = 0; scount--;
                    } else if (src == 0) {
                        sok2 = 0;
                    }
                }
                if ((int)ss2.size() != scount) sok2 = 0;
                for (si2 = 0; si2 < 128; si2++) {
                    sfound = ss2.contains(si2);
                    if (sfound != spresent[si2]) sok2 = 0;
                }
                bd_mix((unsigned int)scount);
                bd_mix((unsigned int)spresent[skey2]);
            }
            bd_check("bd_stress_intset", sok2 && scount >= 0);
            ss2.drop();
        }
        bd_check("bd_stress_terminated", 1);
    }

    /* ---- 9. USE AFTER DROP (double drop inert, zero-init semantics) ------ */
    {
        intmap um = intmap();
        intset us = intset();
        strmap ut = strmap();
        deque ud = deque();
        list ul = list();
        stack uk = stack();
        queue uq = queue();
        int uo = 55, uok;

        um.put(1, 1); um.drop(); um.drop();
        uo = 55;
        uok = um.size() == 0 && um.valid() == 0 && um.get(1, &uo) == -1 &&
              um.remove(1) == -1 && um.contains(1) == 0 && uo == 55;
        uok = uok && um.put(9, 9) == 0 && um.size() == 1 &&
              um.get(9, &uo) == 0 && uo == 9;
        bd_check("bd_drop_intmap", uok);

        us.insert(1); us.drop(); us.drop();
        uok = us.size() == 0 && us.valid() == 0 && !us.contains(1);
        uok = uok && us.insert(9) == 0 && us.size() == 1 && us.contains(9);
        bd_check("bd_drop_intset", uok);

        ut.put("u", 1); ut.drop(); ut.drop();
        uo = 55;
        uok = ut.size() == 0 && ut.valid() == 0 && ut.get("u", &uo) == -1 &&
              ut.remove("u") == -1 && uo == 55;
        uok = uok && ut.put("u", 9) == 0 && ut.size() == 1 &&
              ut.get("u", &uo) == 0 && uo == 9;
        bd_check("bd_drop_strmap", uok);

        ud.push_back(1); ud.drop(); ud.drop();
        uo = 55;
        uok = ud.size() == 0 && ud.capacity() == 0 &&
              ud.pop_front(&uo) == -1 && ud.at_checked(0, &uo) == -1 && uo == 55;
        uok = uok && ud.push_back(9) == 0 && ud.capacity() == 8 &&
              ud.at_checked(0, &uo) == 0 && uo == 9;
        bd_check("bd_drop_deque", uok);

        ul.push_back(1); ul.drop(); ul.drop();
        uo = 55;
        uok = ul.size() == 0 && ul.pop_front(&uo) == -1 &&
              ul.at_checked(0, &uo) == -1 && uo == 55;
        uok = uok && ul.push_back(9) == 0 && ul.size() == 1 &&
              ul.at_checked(0, &uo) == 0 && uo == 9;
        bd_check("bd_drop_list", uok);

        uk.push(1); uk.drop(); uk.drop();
        uo = 55;
        uok = uk.size() == 0 && uk.pop(&uo) == -1 &&
              uk.top_checked(&uo) == -1 && uo == 55;
        uok = uok && uk.push(9) == 0 && uk.top_checked(&uo) == 0 && uo == 9;
        bd_check("bd_drop_stack", uok);

        uq.push(1); uq.drop(); uq.drop();
        uo = 55;
        uok = uq.size() == 0 && uq.pop(&uo) == -1 &&
              uq.front_checked(&uo) == -1 && uo == 55;
        uok = uok && uq.push(9) == 0 && uq.front_checked(&uo) == 0 && uo == 9;
        bd_check("bd_drop_queue", uok);
    }

    /* ---- 10. INVALID ARGUMENTS (all recoverable, no crash) --------------- */
    {
        strmap it = strmap();
        deque idd = deque();
        list il = list();
        stack ik = stack();
        queue iq = queue();
        int io = 77, iok;

        iok = it.put(NULL, 1) == -1 && it.size() == 0 &&
              it.get(NULL, &io) == -1 && io == 77 &&
              it.get("x", NULL) == -1 &&
              it.contains(NULL) == 0 && it.remove(NULL) == -1;
        bd_check("bd_invalid_strmap", iok);

        idd.push_back(5);
        iok = idd.at_checked(SIZE_MAX, &io) == -1 && io == 77 &&
              idd.at_checked(0, NULL) == -1 && idd.size() == 1;
        bd_check("bd_invalid_deque", iok);

        il.push_back(5);
        iok = il.at_checked(SIZE_MAX, &io) == -1 && io == 77 &&
              il.at_checked(0, NULL) == -1 && il.size() == 1;
        bd_check("bd_invalid_list", iok);

        ik.push(5);
        iok = ik.top_checked(NULL) == -1 && ik.pop(NULL) == -1 && ik.size() == 1;
        bd_check("bd_invalid_stack", iok);

        iq.push(5);
        iok = iq.front_checked(NULL) == -1 && iq.pop(NULL) == -1 && iq.size() == 1;
        bd_check("bd_invalid_queue", iok);
    }

    bd_check("bd_no_fatal", 1);
    print("BOUNDARY", bd_checks, bd_failed);
    print("DIGEST", (unsigned int)(bd_digest >> 32),
          (unsigned int)(bd_digest & 0xffffffffULL));
    return bd_failed ? 1 : 0;
}
