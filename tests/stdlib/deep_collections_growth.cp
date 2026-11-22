/* Deep contract tests for stdlib/collections.hp -- growth paths.
 *
 * Covers:
 *   - non-power-of-two capacities passed to grow() (rounded up to a power of 2)
 *   - growth after tombstones, plus reinsertion of removed keys
 *   - empty and NULL keys (strmap)
 *   - deterministic allocation failure during growth (stdlib_alloc_fail_at)
 *   - failure atomicity / no leaks via the checked-allocator telemetry
 *
 * This is a SELF-CHECKING test: every `check()` prints "<label> <1|0>" and
 * increments a failure counter.  The final line is "RESULT <checks> <failed>".
 * Exit code 0 == every check passed; 1 == at least one check failed.
 * The exact expected stdout and the success/recoverable/fatal classification
 * are recorded in docs/stdlib/DEEP_COLLECTIONS_CONTRACT.md.
 *
 * NOTE: the library's internal probe loops are deliberately not instrumented;
 * termination is proven by running this program under an external timeout.
 * Reaching "RESULT" is itself the no-infinite-loop proof.
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

int main(void) {
    intmap m = intmap();
    intmap o = intmap();
    strmap s = strmap();
    strmap t = strmap();
    stdlib_telemetry_stats tm;
    int i, v = 0, ok, present, rc;
    char key[16];

    /* ---- 1. non-power-of-two grow() on an empty map ----------------------- */
    check("g_empty", m.size() == 0 && m.valid() == 0);
    /* next_pow2: 3->8, 7->8(no-op), 9->16, 100->128.  All succeed. */
    check("g_grow3", m.grow(3) == 0 && m.valid() == 1 && m.size() == 0);
    check("g_grow7", m.grow(7) == 0 && m.valid() == 1 && m.size() == 0);
    check("g_grow9", m.grow(9) == 0 && m.valid() == 1 && m.size() == 0);
    check("g_grow100", m.grow(100) == 0 && m.valid() == 1 && m.size() == 0);

    /* Insert 200 keys; capacity grows 128 -> 256. */
    ok = 1;
    for (i = 0; i < 200; i++)
        if (m.put(i * 13, i) != 0)
            ok = 0;
    check("g_insert", ok && m.size() == 200 &&
                          m.get(150 * 13, &v) == 0 && v == 150);

    /* ---- 2. tombstones, no-op grow, real grow, reinsertion ---------------- */
    for (i = 0; i < 200; i += 2)
        m.remove(i * 13); /* 100 tombstones */
    check("g_tomb_remove", m.size() == 100 && m.contains(0) == 0 &&
                               m.contains(26) == 0);
    /* grow(64) rounds to 64 which is <= current 256: documented no-op. */
    check("g_grow_noop", m.grow(64) == 0 && m.size() == 100);
    /* grow(1000) rounds to 1024 > 256: real rebuild, tombstones purged. */
    check("g_grow_big", m.grow(1000) == 0 && m.size() == 100);

    ok = 1;
    for (i = 0; i < 200; i += 2)
        if (m.put(i * 13, i + 1000) != 0)
            ok = 0;
    check("g_reinsert", ok && m.size() == 200);
    check("g_reinsert_vals", m.get(26, &v) == 0 && v == 1002 &&
                                 m.get(198 * 13, &v) == 0 && v == 1198);
    /* Odd keys were never removed, so they kept their original values. */
    check("g_survivors", m.get(13, &v) == 0 && v == 1 &&
                             m.get(199 * 13, &v) == 0 && v == 199);
    present = 0;
    for (i = 0; i < 200; i++)
        if (m.contains(i * 13))
            present++;
    check("g_all_present", present == 200);
    /* An overflowing want is refused recoverably, leaving the map intact. */
    check("g_grow_overflow", m.grow(SIZE_MAX) == -1 && m.size() == 200 &&
                                 m.get(0, &v) == 0);
    m.drop();
    m.drop(); /* idempotent */
    check("g_dropped", m.size() == 0 && m.valid() == 0);

    /* ---- 3. intmap: allocation failure at each rebuild allocation --------- */
    for (i = 0; i < 5; i++)
        o.put(i, i * 10); /* capacity 8, used_slots 5: next put rebuilds */

    stdlib_alloc_fail_at(1);
    ok = 1;
    rc = o.put(100, 999);
    for (i = 0; i < 5; i++) {
        if (o.get(i, &v) != 0 || v != i * 10)
            ok = 0;
    }
    check("g_oom1", rc == -1 && ok && o.size() == 5 && o.valid() == 1 &&
                        o.contains(100) == 0);
    stdlib_alloc_fail_reset();

    stdlib_alloc_fail_at(2);
    ok = 1;
    rc = o.put(100, 999);
    for (i = 0; i < 5; i++) {
        if (o.get(i, &v) != 0 || v != i * 10)
            ok = 0;
    }
    check("g_oom2", rc == -1 && ok && o.size() == 5 && o.valid() == 1 &&
                        o.contains(100) == 0);
    stdlib_alloc_fail_reset();

    stdlib_alloc_fail_at(3);
    ok = 1;
    rc = o.put(100, 999);
    for (i = 0; i < 5; i++) {
        if (o.get(i, &v) != 0 || v != i * 10)
            ok = 0;
    }
    check("g_oom3", rc == -1 && ok && o.size() == 5 && o.valid() == 1 &&
                        o.contains(100) == 0);
    stdlib_alloc_fail_reset();

    check("g_oom_retry", o.put(100, 999) == 0 && o.size() == 6 &&
                             o.get(100, &v) == 0 && v == 999);
    o.drop();

    /* fail_after(0) fails every allocation until reset: a rebuild must be
     * refused recoverably with the map unchanged. */
    {
        intmap ao = intmap();
        for (i = 0; i < 5; i++)
            ao.put(i, i);
        stdlib_alloc_fail_after(0);
        rc = ao.put(100, 999);
        stdlib_alloc_fail_reset();
        check("g_fail_after", rc == -1 && ao.size() == 5 && ao.valid() == 1 &&
                                  ao.contains(100) == 0);
        ao.drop();
    }

    /* ---- 4. strmap: NULL and empty-string keys ---------------------------- */
    check("s_null_put", s.put(NULL, 1) == -1 && s.size() == 0 && s.valid() == 0);
    check("s_null_queries", s.get(NULL, &v) == -1 && s.contains(NULL) == 0 &&
                                s.remove(NULL) == -1);
    check("s_empty_put", s.put("", 7) == 0 && s.size() == 1 &&
                             s.get("", &v) == 0 && v == 7);
    check("s_empty_overwrite", s.put("", 8) == 0 && s.size() == 1 &&
                                   s.get("", &v) == 0 && v == 8);
    check("s_empty_remove", s.remove("") == 0 && s.size() == 0 &&
                                s.get("", &v) == -1 && s.valid() == 1);
    s.drop();

    /* ---- 5. strmap: rebuild succeeds but the key copy fails --------------- */
    for (i = 0; i < 5; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        t.put(key, i);
    }
    /* 3 rebuild allocations succeed, the 4th (key copy) is injected to fail. */
    stdlib_alloc_fail_at(4);
    rc = t.put("newkey", 99);
    stdlib_alloc_fail_reset();
    ok = 1;
    for (i = 0; i < 5; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        if (t.get(key, &v) != 0 || v != i)
            ok = 0;
    }
    check("s_oom_keycopy", rc == -1 && ok && t.size() == 5 && t.valid() == 1 &&
                               t.contains("newkey") == 0);
    check("s_oom_keycopy_retry",
          t.put("newkey", 99) == 0 && t.size() == 6 &&
              t.get("newkey", &v) == 0 && v == 99);
    t.drop();

    /* Each of the three rebuild buffers failing must roll back atomically. */
    {
        strmap ro = strmap();
        for (i = 0; i < 5; i++) {
            snprintf(key, sizeof(key), "r%d", i);
            ro.put(key, i);
        }
        for (i = 1; i <= 3; i++) {
            stdlib_alloc_fail_at(i);
            rc = ro.put("fresh", 77);
            stdlib_alloc_fail_reset();
            ok = 1;
            for (int j = 0; j < 5; j++) {
                snprintf(key, sizeof(key), "r%d", j);
                if (ro.get(key, &v) != 0 || v != j)
                    ok = 0;
            }
            check(i == 1 ? "s_oom1" : (i == 2 ? "s_oom2" : "s_oom3"),
                  rc == -1 && ok && ro.size() == 5 && ro.valid() == 1 &&
                      ro.contains("fresh") == 0);
        }
        ro.drop();
    }

    /* ---- 6. no leaks after a failed growth -------------------------------- */
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);
    {
        intmap lm = intmap();
        for (i = 0; i < 100; i++)
            lm.put(i, i);
        /* Force a 3-buffer rebuild via grow(); fail its second allocation so
         * the partial set is rolled back and the original stays owned. */
        stdlib_alloc_fail_at(2);
        (void)lm.grow(4096);
        stdlib_alloc_fail_reset();
        lm.drop();
    }
    stdlib_telemetry_snapshot(&tm);
    check("g_leak", tm.alloc_count == tm.free_count && tm.alloc_count > 0 &&
                        tm.alloc_failures >= 1);
    stdlib_telemetry_set_enabled(0);

    /* Reaching here with no fatal exit also classifies as "no fatal". */
    check("g_no_fatal", 1);

    print("RESULT", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
