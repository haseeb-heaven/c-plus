/* Upstream-derived regression test for the Microsoft STL vector growth /
 * invariant algorithms ported into stdlib/vector.hp.
 *
 * Provenance (see vector_stl_NOTES.md for full detail):
 *   - vector::_Calculate_growth   <vector>      (subtraction-form overflow
 *     guard, geometric step, demand floor, saturation at max_size)
 *   - _Get_size_of_n<sizeof(int)> <xmemory>     (division-form overflow guard
 *     before the byte-count multiply)
 *   - _Allocation_guard / _Reallocation_guard2  (commit-last mutation)
 *
 * This is a concept/structure adaptation, NOT a verbatim copy: C+ has no
 * templates, exceptions or RAII guards, and deliberately keeps its 2x step,
 * min-4 seed and conservative half-limit refusal policy.
 *
 * Determinism: one fixed-seed LCG drives a bounded (VST_OPS) sequence of
 * push_back / insert / erase / pop_back operations against an in-test reference
 * model.  No wall-clock, randomness source, environment or game asset is read.
 *
 * Records: one `<tag> <0|1>` per targeted check, then
 * `VECTOR_STL <checks> <failures>`.  Exit 0 only when failures == 0.
 */
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

#define VST_SEED 0x5EEDu
#define VST_OPS 2048
#define VST_MODEL_MAX 512

static int vst_checks = 0;
static int vst_failures = 0;

static void vst(const char *tag, int ok) {
    vst_checks++;
    if (!ok)
        vst_failures++;
    print(tag, ok ? 1 : 0);
}

int main(void) {
    size_t max_count = STDLIB_ALLOC_LIMIT / sizeof(int);
    size_t bytes = 0;
    int out = -1;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(0);

    /* ---- 1. _Get_size_of_n-style checked multiplication ---------------- */
    vst("VST_SIZE_OF_N_EXACT",
        vector_allocation_bytes(max_count, &bytes) == 0 &&
            bytes == sizeof(int) * max_count);
    vst("VST_SIZE_OF_N_OVERFLOW",
        vector_allocation_bytes(max_count + 1, &bytes) == -1);
    vst("VST_SIZE_OF_N_ZERO",
        vector_allocation_bytes(0, &bytes) == 0 && bytes == 0);

    /* ---- 2. _Calculate_growth structure (called directly, no allocation) */
    vst("VST_GROWTH_SEED", vector_calculate_growth(0, 4) == 4);
    vst("VST_GROWTH_GEOMETRIC", vector_calculate_growth(4, 5) == 8);
    vst("VST_GROWTH_DEMAND_FLOOR", vector_calculate_growth(4, 9) == 9);
    vst("VST_GROWTH_HALF_BOUNDARY",
        vector_calculate_growth(max_count / 2, (max_count / 2) + 1) == max_count);
    vst("VST_GROWTH_SATURATION",
        vector_calculate_growth(max_count - 1, max_count) == max_count);
    vst("VST_GROWTH_AT_CEILING",
        vector_calculate_growth(max_count, max_count + 1) == max_count);

    /* ---- 3. Public growth sequence is unchanged (4 -> 8 -> 16) --------- */
    {
        vector g = vector();
        int i;
        int ok = 1;
        for (i = 0; i < 4; i++)
            if (g.push_back(i) != 0)
                ok = 0;
        if (g.capacity() != 4)
            ok = 0;
        if (g.push_back(4) != 0 || g.capacity() != 8)
            ok = 0;
        for (i = 5; i < 9; i++)
            if (g.push_back(i) != 0)
                ok = 0;
        if (g.capacity() != 16 || g.size() != 9)
            ok = 0;
        vst("VST_CAPACITY_SEQUENCE", ok);
        g.drop();
    }

    /* ---- 4. Overflow refusal via public API is failure-atomic --------- */
    {
        vector r = vector();
        int ok = 1;
        if (r.push_back(7) != 0 || r.capacity() != 4)
            ok = 0;
        if (r.reserve(SIZE_MAX) != -1 || r.capacity() != 4 || r.size() != 1)
            ok = 0;
        if (r.reserve(max_count + 1) != -1 || r.capacity() != 4 || r.size() != 1)
            ok = 0;
        if (r.at_checked(0, &out) != 0 || out != 7)
            ok = 0;
        vst("VST_RESERVE_OVERFLOW_ATOMIC", ok);
        r.drop();
    }

    /* ---- 5. Commit-last: failed growth leaves vector byte-for-byte equal */
    {
        vector a = vector();
        slice s;
        int i;
        int ok = 1;
        int rc;
        long seq0;
        for (i = 0; i < 4; i++)
            if (a.push_back(i) != 0)
                ok = 0;
        s = a.view();
        if (s.stale() || !s.valid())
            ok = 0;

        stdlib_alloc_fail_reset();
        stdlib_alloc_fail_at(1); /* the growth allocation fails once */
        seq0 = stdlib_alloc_seq();
        rc = a.push_back(99);
        if (rc != -1)
            ok = 0;
        if (a.size() != 4 || a.capacity() != 4)
            ok = 0;
        if (a.at_checked(3, &out) != 0 || out != 3)
            ok = 0;
        if (s.stale())
            ok = 0; /* no generation bump on the failed path */
        if (stdlib_alloc_seq() != seq0 + 1)
            ok = 0; /* one attempt, no leaked extra allocation */
        stdlib_alloc_fail_reset();
        vst("VST_COMMIT_LAST_FAILED_ATOMIC", ok);

        /* Retry after the injected fault is cleared: growth commits and the
         * previously taken slice is invalidated exactly once. */
        ok = 1;
        if (a.push_back(99) != 0 || a.size() != 5 || a.capacity() != 8)
            ok = 0;
        if (a.at_checked(4, &out) != 0 || out != 99)
            ok = 0;
        if (!s.stale() || s.valid() || s.at_checked(0, &out) != -1)
            ok = 0; /* stale slice refuses reads without dereferencing */
        vst("VST_COMMIT_LAST_RETRY_PUBLISHES", ok);
        a.drop();
    }

    /* ---- 6. Fixed-seed bounded operation/model comparison ------------- */
    {
        vector d = vector();
        /* One-based model: element k lives at model[k+1], so the last element
         * is model[model_len] and no subtraction on model_len is needed. */
        int model[VST_MODEL_MAX + 1];
        size_t model_len = 0;
        unsigned long long state = VST_SEED;
        size_t last_cap = 0;
        int ok = 1;
        int step;

        for (step = 0; step < VST_OPS; step++) {
            size_t op;
            size_t idx = 0;
            int value;
            size_t j;

            state = state * 1103515245ULL + 12345ULL;
            op = (size_t)((state >> 16) & 3ULL);
            value = (int)(state & 0xffffULL);

            if (op == 0 && model_len < VST_MODEL_MAX) { /* push_back */
                if (d.push_back(value) != 0)
                    ok = 0;
                model_len++;
                model[model_len] = value;
            } else if (op == 1 && model_len < VST_MODEL_MAX) { /* insert */
                idx = (size_t)((state >> 24) & 0x1ffULL);
                if (idx > model_len)
                    idx = model_len; /* clamp into [0, model_len] */
                if (d.insert(idx, value) != 0)
                    ok = 0;
                for (j = model_len + 1; j > idx + 1; j--)
                    model[j] = model[j - 1];
                model_len++;
                model[idx + 1] = value;
            } else if (op == 2 && model_len > 0) { /* erase */
                int erased = -1;
                idx = (size_t)((state >> 24) & 0x1ffULL);
                if (idx >= model_len)
                    idx = 0; /* clamp into [0, model_len-1] */
                if (d.erase(idx, &erased) != 0 || erased != model[idx + 1])
                    ok = 0;
                for (j = idx + 1; j < model_len; j++)
                    model[j] = model[j + 1];
                model_len--;
            } else if (op == 3 && model_len > 0) { /* pop_back */
                int popped = -1;
                if (d.pop_back(&popped) != 0 || popped != model[model_len])
                    ok = 0;
                model_len--;
            }

            if (d.size() != model_len)
                ok = 0;
            if (d.capacity() < last_cap)
                ok = 0; /* capacity is monotonic under push/insert */
            last_cap = d.capacity();
            for (j = 0; j < model_len; j++) {
                int got = -1;
                if (d.at_checked(j, &got) != 0 || got != model[j + 1]) {
                    ok = 0;
                    break;
                }
            }
            if (!ok)
                break;
        }
        vst("VST_DETERMINISTIC_MODEL", ok && model_len > 0);
        d.drop();
    }

    print("VECTOR_STL", vst_checks, vst_failures);
    return vst_failures == 0 ? 0 : 1;
}
