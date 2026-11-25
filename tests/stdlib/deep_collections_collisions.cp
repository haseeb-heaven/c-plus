/* Deep contract tests for stdlib/collections.hp -- collision-heavy paths.
 *
 * Covers:
 *   - collision-heavy insertions (keys sharing the low hash bits so they keep
 *     colliding through several growth steps)
 *   - removal and reinsertion (tombstones in the middle of a probe chain)
 *   - growth after tombstones (long chains survive rehash)
 *   - intmap, intset and strmap (owned keys) under the same stress
 *   - termination of every probing loop (external timeout + reaching RESULT)
 *
 * SELF-CHECKING: each check() prints "<label> <1|0>"; the final line is
 * "RESULT <checks> <failed>".  Exit 0 == all passed, 1 == a check failed.
 * Exact expected stdout and classification live in
 * docs/stdlib/DEEP_COLLECTIONS_CONTRACT.md.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

static int g_checks = 0;
static int g_failed = 0;
static int g_intkeys[96];
static char g_strkeys[48][16];
static unsigned long long rng_state = 0ULL;

/* Fixed-seed LCG so the differential stress is fully reproducible. */
static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

static void check(const char *label, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(label, cond ? 1 : 0);
}

/* Collect `need` distinct int keys whose hash low bits (under `mask`) are all
 * zero.  Such keys collide at every table capacity <= mask+1, so a chain built
 * at capacity 8 survives growth up to mask+1. */
static int collect_int_keys(unsigned long long mask, long long start, int need,
                            int out_offset) {
    long long k = start;
    int found = 0;
    while (found < need && k < 8000000LL) {
        if ((stdlib_map_hash_int(k) & mask) == 0ULL)
            g_intkeys[out_offset + found++] = (int)k;
        k++;
    }
    return found;
}

/* Collect `need` short strings whose FNV hash low 6 bits are zero. */
static int collect_str_keys(int need) {
    int found = 0;
    int i;
    for (i = 0; found < need && i < 500000; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "s%d", i);
        if ((stdlib_map_hash_text(buf) & 63ULL) == 0ULL) {
            snprintf(g_strkeys[found], 16, "%s", buf);
            found++;
        }
    }
    return found;
}

int main(void) {
    intmap m = intmap();
    intset s = intset();
    strmap sm = strmap();
    int i, v = 0, ok, n;

    /* ---- 1. build a 48-key chain that collides at every cap <= 1024 ------- */
    n = collect_int_keys(1023ULL, 0LL, 48, 0);
    check("c_collect_int", n == 48);

    ok = 1;
    for (i = 0; i < 48; i++)
        if (m.put(g_intkeys[i], 1000 + i) != 0)
            ok = 0;
    check("c_map_insert", ok && m.size() == 48);
    ok = 1;
    for (i = 0; i < 48; i++)
        if (m.get(g_intkeys[i], &v) != 0 || v != 1000 + i)
            ok = 0;
    check("c_map_probe_all", ok); /* all 48 found through the same chain */

    /* ---- 2. removal and reinsertion inside the chain --------------------- */
    for (i = 0; i < 48; i += 2)
        m.remove(g_intkeys[i]); /* 24 tombstones */
    ok = 1;
    for (i = 0; i < 48; i += 2)
        if (m.contains(g_intkeys[i]))
            ok = 0;
    check("c_map_removed", ok && m.size() == 24);
    ok = 1;
    for (i = 1; i < 48; i += 2)
        if (m.get(g_intkeys[i], &v) != 0 || v != 1000 + i)
            ok = 0;
    check("c_map_kept", ok);

    ok = 1;
    for (i = 0; i < 48; i += 2)
        if (m.put(g_intkeys[i], 2000 + i) != 0)
            ok = 0;
    check("c_map_reinsert", ok && m.size() == 48);
    ok = 1;
    for (i = 0; i < 48; i++) {
        int expect = (i % 2 == 0) ? 2000 + i : 1000 + i;
        if (m.get(g_intkeys[i], &v) != 0 || v != expect)
            ok = 0;
    }
    check("c_map_values", ok);

    /* ---- 3. grow the collided table past its initial capacity ------------ */
    /* Append 16 more colliding keys (distinct from the first 48). */
    n = collect_int_keys(1023ULL, (long long)g_intkeys[47] + 1LL, 16, 48);
    check("c_collect_more", n == 16);
    ok = 1;
    for (i = 48; i < 64; i++)
        if (m.put(g_intkeys[i], 3000 + i) != 0)
            ok = 0;
    check("c_map_grow", ok && m.size() == 64);
    ok = 1;
    for (i = 0; i < 64; i++) {
        int expect;
        if (i < 48)
            expect = (i % 2 == 0) ? 2000 + i : 1000 + i;
        else
            expect = 3000 + i;
        if (m.get(g_intkeys[i], &v) != 0 || v != expect)
            ok = 0;
    }
    check("c_map_grow_probe", ok); /* 64-way chain, rehashed several times */
    m.drop();
    m.drop();
    check("c_map_dropped", m.size() == 0 && m.valid() == 0);

    /* ---- 4. intset under the same collisions ----------------------------- */
    for (i = 0; i < 48; i++)
        s.insert(g_intkeys[i]);
    check("c_set_insert", s.size() == 48);
    for (i = 0; i < 48; i += 2)
        s.remove(g_intkeys[i]);
    ok = 1;
    for (i = 0; i < 48; i += 2)
        if (s.contains(g_intkeys[i]))
            ok = 0;
    check("c_set_removed", ok && s.size() == 24);
    for (i = 0; i < 48; i += 2)
        s.insert(g_intkeys[i]);
    ok = 1;
    for (i = 0; i < 48; i++)
        if (!s.contains(g_intkeys[i]))
            ok = 0;
    check("c_set_reinsert", ok && s.size() == 48);
    s.drop();

    /* ---- 5. strmap (owned keys) under collisions ------------------------- */
    n = collect_str_keys(40);
    check("c_collect_str", n == 40);
    ok = 1;
    for (i = 0; i < 40; i++)
        if (sm.put(g_strkeys[i], i) != 0)
            ok = 0;
    check("c_str_insert", ok && sm.size() == 40);
    ok = 1;
    for (i = 0; i < 40; i++)
        if (sm.get(g_strkeys[i], &v) != 0 || v != i)
            ok = 0;
    check("c_str_probe_all", ok);
    for (i = 0; i < 40; i += 2)
        sm.remove(g_strkeys[i]);
    ok = 1;
    for (i = 0; i < 40; i += 2)
        if (sm.contains(g_strkeys[i]))
            ok = 0;
    check("c_str_removed", ok && sm.size() == 20);
    ok = 1;
    for (i = 1; i < 40; i += 2)
        if (sm.get(g_strkeys[i], &v) != 0 || v != i)
            ok = 0;
    check("c_str_kept", ok);
    ok = 1;
    for (i = 0; i < 40; i += 2)
        if (sm.put(g_strkeys[i], 100 + i) != 0)
            ok = 0;
    check("c_str_reinsert", ok && sm.size() == 40);
    ok = 1;
    for (i = 0; i < 40; i++) {
        int expect = (i % 2 == 0) ? 100 + i : i;
        if (sm.get(g_strkeys[i], &v) != 0 || v != expect)
            ok = 0;
    }
    check("c_str_values", ok);
    sm.drop();

    /* ---- 6. deterministic differential stress against a reference model -- */
    {
        intmap dm = intmap();
        int model_val[48];
        int model_present[48];
        int model_count = 0;
        int bad = 0;
        int iter;
        for (i = 0; i < 48; i++)
            model_present[i] = 0;
        rng_state = 0x9E3779B9ULL;
        for (iter = 0; iter < 2000; iter++) {
            unsigned int r = rnd();
            int idx = (int)(r % 48U);
            int key = g_intkeys[idx];
            if (((r >> 8) % 3U) == 0U) {
                int rc = dm.remove(key);
                if (model_present[idx]) {
                    if (rc != 0)
                        bad = 1;
                    model_present[idx] = 0;
                    model_count--;
                } else if (rc == 0) {
                    bad = 1;
                }
            } else {
                int val = (int)((r >> 16) & 0xFFFFU);
                int rc = dm.put(key, val);
                if (rc != 0)
                    bad = 1;
                if (!model_present[idx]) {
                    model_present[idx] = 1;
                    model_count++;
                }
                model_val[idx] = val;
            }
        }
        for (i = 0; i < 48; i++) {
            int got = 0;
            int found = dm.get(g_intkeys[i], &got) == 0;
            if (found != model_present[i])
                bad = 1;
            else if (found && got != model_val[i])
                bad = 1;
        }
        check("c_diff_model", bad == 0 && (int)dm.size() == model_count);
        dm.drop();
    }

    /* Reaching RESULT under a timeout proves no infinite probing loop. */
    check("c_terminated", 1);
    print("RESULT", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
