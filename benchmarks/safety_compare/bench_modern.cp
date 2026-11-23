/* bench_modern.cp -- deterministic C+ modern-stdlib benchmark harness.
 *
 * Workload (identical source for every build variant, so comparisons are fair):
 *   - vector: N = 2,000,000 push_back() calls (geometric growth through the
 *     checked stdlib_alloc) followed by N bounds-checked at() reads.
 *   - string: M = 200,000 append() calls followed by size() reads.
 *
 * Determinism: a fixed-seed xorshift32 PRNG drives the values pushed into the
 * vector, the pseudo-random (always in-range) indices read back with at(), and
 * the short chunks appended to the string.  Re-running the binary always
 * emits the identical checksum, printed together with per-phase timing as a
 * single CSV row.
 *
 * Implementation note (honest scope): the string side uses the real modern
 * stdlib type stdlib/string.hp.  The vector side uses a self-contained checked
 * growable vector defined below that calls the SAME checked allocator
 * (stdlib_alloc / stdlib_release from stdlib/error.hp) and exposes the same
 * push_back()/at() API as stdlib/vector.hp.  This is because, at the time of
 * measurement, stdlib/vector.hp carried a concurrent, unverified edit whose
 * scalar out-parameter helper trips the C+ definite-init analyzer in *every*
 * build mode (exact error recorded in bench_modern_NOTES.md).  Using the
 * checked allocator directly keeps the allocation-heavy workload intact and
 * lets the harness build under the required --strict --sanitize=none mode.
 *
 * Build: ./build/c+ build --strict --sanitize=none benchmarks/safety_compare/bench_modern.cp -o <out>
 */
#include <stdio.h>
#include <time.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define N 2000000
#define M 200000

/* Fixed, non-zero seed (golden-ratio increment).  Never derived from time. */
#define BENCH_SEED 0x9E3779B9u

/* ---- self-contained checked growable int vector (stdlib allocator) ------- */
class bench_vector {
    int *items;
    size_t used;
    size_t space;

    bench_vector(void) {
        this->items = NULL;
        this->used = 0;
        this->space = 0;
    }

    int push_back(int value) {
        if (this->used >= this->space) {
            size_t want = this->space == 0 ? 4 : this->space * 2;
            int *fresh = (int *)stdlib_alloc(sizeof(int) * want);
            size_t i;
            if (fresh == NULL)
                return -1;
            for (i = 0; i < this->used; i++)
                fresh[i] = this->items[i];
            if (this->items != NULL)
                stdlib_release(this->items);
            this->items = fresh;
            this->space = want;
        }
        if (this->used >= this->space)
            return -1;
        this->items[this->used] = value;
        this->used++;
        return 0;
    }

    /* Checked read: out-of-range returns 0 (as stdlib/vector.hp::at does). */
    int at(size_t index) {
        if (index >= this->used)
            return 0;
        return this->items[index];
    }

    void drop(void) {
        if (this->items != NULL) {
            stdlib_release(this->items);
            this->items = NULL;
        }
    }
};

static unsigned int rng_state = BENCH_SEED;

static unsigned int next_u32(void) {
    unsigned int x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static double now_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int main(void) {
    long long vsum = 0;
    long long ssize_sum = 0;
    unsigned long long checksum;
    double t0, t1, t2;
    double vector_us, string_us, total_us;
    bench_vector v = bench_vector();
    string s = string("a");
    int i;

    /* --- vector phase: N push_back + N at() reads --- */
    t0 = now_seconds();
    for (i = 0; i < N; i++) {
        int val = (int)(next_u32() & 0x7FFFFFFFu);
        if (v.push_back(val) != 0)
            return 1;
    }
    for (i = 0; i < N; i++) {
        size_t idx = (size_t)(next_u32() % (unsigned int)N);
        vsum += (long long)v.at(idx);
    }
    t1 = now_seconds();

    /* --- string phase: M appends + M size() reads --- */
    for (i = 0; i < M; i++) {
        unsigned int r = (next_u32() % 4u) + 1u;
        char chunk[8];
        unsigned int k;
        for (k = 0; k < r; k++)
            chunk[k] = (char)('a' + (char)(next_u32() % 26u));
        chunk[r] = '\0';
        if (s.append(chunk) != 0)
            return 1;
        ssize_sum += (long long)s.size();
    }
    t2 = now_seconds();

    vector_us = (t1 - t0) * 1000000.0;
    string_us = (t2 - t1) * 1000000.0;
    total_us  = (t2 - t0) * 1000000.0;

    checksum = (unsigned long long)vsum ^
               ((unsigned long long)ssize_sum << 17) ^
               (unsigned long long)N ^
               ((unsigned long long)M << 32);

    /* Machine-readable output: header + one CSV data row. */
    printf("checksum,vector_us,string_us,total_us\n");
    printf("%llu,%.3f,%.3f,%.3f\n", checksum, vector_us, string_us, total_us);
    return 0;
}
