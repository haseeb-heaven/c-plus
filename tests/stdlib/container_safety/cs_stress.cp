/* Container-safety regression suite -- fixed-seed stress:
 *   collisions, rehash, duplicate/missing keys, repeated insert/remove cycles,
 *   large data volumes, and a differential model for intmap.
 *
 * SELF-CHECKING: "<tag> <1|0>" per check (1 = pass); final line
 * "CONTAINER_SAFETY <checks> <failures>".  Every loop has a hard bound and the
 * PRNG seed is fixed, so output is deterministic and the run is safe under an
 * external timeout.  Reaching the final line proves no probe/rehash loop hung.
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

/* Fixed-seed LCG: all "random" choices below are reproducible. */
static unsigned long long g_rng = 0ULL;
static unsigned int rnd(void) {
    g_rng = g_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(g_rng >> 33);
}

static int g_ikeys[64];
static char g_skeys[40][16];
static int g_present[64];
static int g_val[64];

/* Keys whose hash low 6 bits are zero collide at every capacity <= 64. */
static int collect_ikeys(void) {
    long long k = 0;
    int found = 0;
    while (found < 64 && k < 200000LL) {
        if ((stdlib_map_hash_int(k) & 63ULL) == 0ULL)
            g_ikeys[found++] = (int)k;
        k++;
    }
    return found;
}

static int collect_skeys(void) {
    int i, found = 0;
    for (i = 0; found < 40 && i < 200000; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "s%d", i);
        if ((stdlib_map_hash_text(buf) & 63ULL) == 0ULL) {
            snprintf(g_skeys[found], 16, "%s", buf);
            found++;
        }
    }
    return found;
}

int main(void) {
    intmap cs_map_stress = intmap();
    intmap cs_map_cycle = intmap();
    intset cs_set_stress = intset();
    strmap cs_smap_stress = strmap();
    vector cs_vec_stress = vector();
    deque cs_deq_stress = deque();
    list cs_list_stress = list();
    stack cs_stack_stress = stack();
    queue cs_queue_stress = queue();
    stdlib_telemetry_stats cs_tm;
    char cs_key[24];
    int out = 0, i, iter, n, ok, bad, gcount;

    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);

    /* ---- 1. collision-heavy intmap: insert, remove, reinsert, rehash ----- */
    n = collect_ikeys();
    check("s_collect_int", n == 64);
    ok = 1;
    for (i = 0; i < 64; i++)
        if (cs_map_stress.put(g_ikeys[i], 1000 + i) != 0)
            ok = 0;
    check("s_map_insert", ok && cs_map_stress.size() == 64);
    ok = 1;
    for (i = 0; i < 64; i++)
        if (cs_map_stress.get(g_ikeys[i], &out) != 0 || out != 1000 + i)
            ok = 0;
    check("s_map_probe", ok); /* all 64 found through the same probe chain */

    for (i = 0; i < 64; i += 2)
        cs_map_stress.remove(g_ikeys[i]);
    ok = 1;
    for (i = 0; i < 64; i += 2)
        if (cs_map_stress.contains(g_ikeys[i]))
            ok = 0;
    check("s_map_removed", ok && cs_map_stress.size() == 32);
    ok = 1;
    for (i = 1; i < 64; i += 2)
        if (cs_map_stress.get(g_ikeys[i], &out) != 0 || out != 1000 + i)
            ok = 0;
    check("s_map_kept", ok);

    ok = 1;
    for (i = 0; i < 64; i += 2)
        if (cs_map_stress.put(g_ikeys[i], 2000 + i) != 0)
            ok = 0;
    check("s_map_reinsert", ok && cs_map_stress.size() == 64);
    ok = 1;
    for (i = 0; i < 64; i++) {
        int want = (i % 2 == 0) ? 2000 + i : 1000 + i;
        if (cs_map_stress.get(g_ikeys[i], &out) != 0 || out != want)
            ok = 0;
    }
    check("s_map_values", ok);

    check("s_map_rehash", cs_map_stress.grow(2048) == 0 && cs_map_stress.size() == 64);
    ok = 1;
    for (i = 0; i < 64; i++) {
        int want = (i % 2 == 0) ? 2000 + i : 1000 + i;
        if (cs_map_stress.get(g_ikeys[i], &out) != 0 || out != want)
            ok = 0;
    }
    check("s_map_rehash_probe", ok);

    /* ---- 2. differential model, 4000 bounded fixed-seed operations ------- */
    cs_map_stress.clear();
    for (i = 0; i < 64; i++) {
        g_present[i] = 0;
        g_val[i] = 0;
    }
    gcount = 0;
    bad = 0;
    g_rng = 0x9E3779B9ULL;
    for (iter = 0; iter < 4000; iter++) {
        unsigned int r = rnd();
        int idx = (int)(r & 63U);
        int key = g_ikeys[idx];
        if (((r >> 8) & 3U) == 0U) {
            int rc = cs_map_stress.remove(key);
            if (g_present[idx]) {
                if (rc != 0)
                    bad = 1;
                g_present[idx] = 0;
                gcount--;
            } else if (rc == 0) {
                bad = 1;
            }
        } else {
            int val = (int)((r >> 16) & 0xFFFFU);
            int rc = cs_map_stress.put(key, val);
            if (rc != 0)
                bad = 1;
            if (!g_present[idx]) {
                g_present[idx] = 1;
                gcount++;
            }
            g_val[idx] = val;
        }
    }
    for (i = 0; i < 64; i++) {
        int found = cs_map_stress.get(g_ikeys[i], &out) == 0;
        if (found != g_present[i])
            bad = 1;
        else if (found && out != g_val[i])
            bad = 1;
    }
    check("s_diff_model", bad == 0 && (int)cs_map_stress.size() == gcount);
    cs_map_stress.drop();

    /* ---- 3. repeated insert/remove cycles (tombstone churn) -------------- */
    bad = 0;
    for (iter = 0; iter < 200; iter++) {
        for (i = 0; i < 32; i++)
            if (cs_map_cycle.put(i * 3, i) != 0)
                bad = 1;
        if (cs_map_cycle.size() != 32)
            bad = 1;
        for (i = 0; i < 32; i++)
            if (cs_map_cycle.remove(i * 3) != 0)
                bad = 1;
        if (cs_map_cycle.size() != 0 || cs_map_cycle.contains(0))
            bad = 1;
    }
    check("s_cycles", bad == 0 && cs_map_cycle.size() == 0);
    cs_map_cycle.drop();

    /* ---- 4. large volumes: vector ---------------------------------------- */
    ok = 1;
    for (i = 0; i < 4096; i++)
        if (cs_vec_stress.push_back(i) != 0)
            ok = 0;
    check("s_vec_bulk", ok && cs_vec_stress.size() == 4096 &&
                         cs_vec_stress.capacity() >= 4096 &&
                         cs_vec_stress.at_checked(4095, &out) == 0 && out == 4095);
    ok = 1;
    for (i = 0; i < 4096; i++)
        if (cs_vec_stress.pop_back(&out) != 0 || out != 4095 - i)
            ok = 0;
    check("s_vec_drain", ok && cs_vec_stress.size() == 0);
    cs_vec_stress.drop();

    /* ---- 5. large volumes: intset / strmap ------------------------------- */
    ok = 1;
    for (i = 0; i < 2048; i++)
        if (cs_set_stress.insert(i * 13 + 1) != 0)
            ok = 0;
    check("s_set_bulk", ok && cs_set_stress.size() == 2048 &&
                         cs_set_stress.contains(13 * 2047 + 1) == 1);
    ok = 1;
    for (i = 0; i < 2048; i++)
        if (cs_set_stress.remove(i * 13 + 1) != 0)
            ok = 0;
    check("s_set_drain", ok && cs_set_stress.size() == 0);
    cs_set_stress.drop();

    n = collect_skeys();
    check("s_collect_str", n == 40);
    ok = 1;
    for (i = 0; i < 40; i++)
        if (cs_smap_stress.put(g_skeys[i], i) != 0)
            ok = 0;
    check("s_str_collisions", ok && cs_smap_stress.size() == 40);
    ok = 1;
    for (i = 0; i < 40; i++)
        if (cs_smap_stress.get(g_skeys[i], &out) != 0 || out != i)
            ok = 0;
    check("s_str_probe", ok);
    for (i = 0; i < 1000; i++) {
        snprintf(cs_key, sizeof(cs_key), "bulk%d", i);
        if (cs_smap_stress.put(cs_key, 1000 + i) != 0)
            ok = 0;
    }
    check("s_str_bulk", ok && cs_smap_stress.size() == 1040);
    snprintf(cs_key, sizeof(cs_key), "bulk%d", 999);
    check("s_str_bulk_probe", cs_smap_stress.get(cs_key, &out) == 0 && out == 1999);
    cs_smap_stress.drop();

    /* ---- 6. large volumes: wrapped deque / list / stack / queue --------- */
    ok = 1;
    for (i = 0; i < 2048; i++)
        if (cs_deq_stress.push_back(i) != 0)
            ok = 0;
    for (i = 0; i < 2048; i++)
        if (cs_deq_stress.push_front(i) != 0)
            ok = 0;
    check("s_deq_bulk", ok && cs_deq_stress.size() == 4096 &&
                         cs_deq_stress.at_checked(0, &out) == 0 && out == 2047 &&
                         cs_deq_stress.at_checked(2047, &out) == 0 && out == 0 &&
                         cs_deq_stress.at_checked(2048, &out) == 0 && out == 0 &&
                         cs_deq_stress.at_checked(4095, &out) == 0 && out == 2047);
    ok = 1;
    for (i = 0; i < 4096; i++)
        if (cs_deq_stress.pop_front(&out) != 0)
            ok = 0;
    check("s_deq_drain", ok && cs_deq_stress.size() == 0);
    cs_deq_stress.drop();

    ok = 1;
    for (i = 0; i < 1024; i++)
        if (cs_list_stress.push_back(i) != 0)
            ok = 0;
    for (i = 0; i < 1024; i++)
        if (cs_list_stress.push_front(i) != 0)
            ok = 0;
    check("s_list_bulk", ok && cs_list_stress.size() == 2048 &&
                          cs_list_stress.at_checked(0, &out) == 0 && out == 1023 &&
                          cs_list_stress.at_checked(1023, &out) == 0 && out == 0 &&
                          cs_list_stress.at_checked(1024, &out) == 0 && out == 0 &&
                          cs_list_stress.at_checked(2047, &out) == 0 && out == 1023);
    ok = 1;
    for (i = 0; i < 2048; i++)
        if (cs_list_stress.pop_back(&out) != 0)
            ok = 0;
    check("s_list_drain", ok && cs_list_stress.size() == 0);
    cs_list_stress.drop();

    ok = 1;
    for (i = 0; i < 2048; i++)
        if (cs_stack_stress.push(i) != 0)
            ok = 0;
    for (i = 0; i < 2048; i++)
        if (cs_stack_stress.pop(&out) != 0 || out != 2047 - i)
            ok = 0;
    check("s_stack_bulk", ok && cs_stack_stress.size() == 0);
    cs_stack_stress.drop();

    ok = 1;
    for (i = 0; i < 2048; i++)
        if (cs_queue_stress.push(i) != 0)
            ok = 0;
    for (i = 0; i < 2048; i++)
        if (cs_queue_stress.pop(&out) != 0 || out != i)
            ok = 0;
    check("s_queue_bulk", ok && cs_queue_stress.size() == 0);
    cs_queue_stress.drop();

    stdlib_telemetry_snapshot(&cs_tm);
    check("s_growth_seen", cs_tm.container_growths > 0 && cs_tm.alloc_count > 0);
    check("s_terminated", 1);
    stdlib_telemetry_set_enabled(0);
    print("CONTAINER_SAFETY", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
