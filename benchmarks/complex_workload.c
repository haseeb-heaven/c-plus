/* A multi-phase, long-running benchmark for the C+ compiler and runtime.
 *
 * Unlike the short microbenchmarks in this directory, this program exercises
 * four workloads that interact with different parts of the runtime:
 *
 *   1. matrix   - dense arithmetic with bounds-checked indexing
 *   2. hash     - many small managed/plain allocations in bucket chains
 *   3. sort     - comparator calls over a large array
 *   4. arena    - bump allocation with periodic rewind
 *
 * Every phase ends with a deterministic checksum. The benchmark runner builds
 * the same source three ways (c+ --release, c+ --release --legacy-c and plain
 * clang -O2) and verifies that all three produce identical checksums before it
 * compares timings. Optimizing away a phase therefore cannot look like a win.
 *
 * Work is scaled by CPLUS_BENCH_SCALE (default 100). Each phase prints its own
 * elapsed nanoseconds so regressions can be attributed to a specific workload.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cplus/cplus.h"

#define SCALE_DEFAULT 100u

/* Per-scale base work amounts. Chosen so the default run lasts several
 * minutes in total while each phase remains long enough to average out
 * scheduler noise. */
#define MATRIX_DIM 96u
#define MATRIX_ROUNDS_PER_SCALE 120u
#define HASH_SLOTS 4096u
#define HASH_ITEMS_PER_SCALE 700u
#define SORT_ITEMS_PER_SCALE 320u
#define ARENA_BLOCK_BYTES (1u << 20)
#define ARENA_RECORD_BYTES 64u
#define ARENA_ROUNDS_PER_SCALE 60u

static uint64_t seed = 0x9e3779b97f4a7c15ULL;

static uint64_t next_random(void) {
    /* SplitMix64: deterministic across all three compile modes. */
    seed += 0x9e3779b97f4a7c15ULL;
    uint64_t z = seed;
    z = (z ^ (z >> 30u)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27u)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31u);
}

static uint64_t mix(uint64_t hash, uint64_t value) {
    return (hash ^ value) * 0x100000001b3ULL;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "clock_gettime failed\n");
        exit(2);
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static size_t checked(size_t index, size_t count, const char *what) {
#ifdef CPLUS
    return (size_t)cplus_check_index(index, count, __FILE__, __LINE__, what);
#else
    (void)what;
    return index < count ? index : count - 1u;
#endif
}

static void *alloc_record(size_t bytes) {
#ifdef CPLUS
    return cplus_alloc(bytes);
#else
    return malloc(bytes);
#endif
}

static void release_record(void *ptr) {
#ifdef CPLUS
    cplus_release(ptr);
#else
    free(ptr);
#endif
}

/* Phase 1: dense matrix multiplication with explicit index checks. */
static uint64_t phase_matrix(uint32_t scale) {
    const size_t n = MATRIX_DIM;
    const size_t cells = n * n;
    const uint32_t rounds = MATRIX_ROUNDS_PER_SCALE * scale;
    uint64_t checksum = 0xcbf29ce484222325ULL;
    double *a = (double *)calloc(cells, sizeof(*a));
    double *b = (double *)calloc(cells, sizeof(*b));
    double *c = (double *)calloc(cells, sizeof(*c));
    if (!a || !b || !c) {
        fprintf(stderr, "matrix allocation failed\n");
        exit(2);
    }

    for (size_t k = 0; k < cells; ++k) {
        a[k] = (double)(next_random() % 1000u) / 100.0;
        b[k] = (double)(next_random() % 1000u) / 100.0;
    }
    for (uint32_t round = 0; round < rounds; ++round) {
        for (size_t row = 0; row < n; ++row) {
            for (size_t col = 0; col < n; ++col) {
                double sum = 0.0;
                for (size_t k = 0; k < n; ++k) {
                    size_t ai = checked(row * n + k, cells, "a");
                    size_t bi = checked(k * n + col, cells, "b");
                    sum += a[ai] * b[bi];
                }
                c[checked(row * n + col, cells, "c")] = sum;
            }
        }
        checksum = mix(checksum, (uint64_t)c[checked(round % cells, cells, "c")]);
    }
    free(a);
    free(b);
    free(c);
    return checksum;
}

/* Phase 2: bucket chains built from many short-lived allocations. */
static uint64_t phase_hash(uint32_t scale) {
    const uint32_t items = HASH_ITEMS_PER_SCALE * scale;
    typedef struct Entry {
        uint64_t key;
        uint32_t value;
        struct Entry *next;
    } Entry;
    Entry **buckets = (Entry **)calloc(HASH_SLOTS, sizeof(*buckets));
    Entry **entries = (Entry **)malloc((size_t)items * sizeof(*entries));
    if (!buckets || !entries) {
        fprintf(stderr, "hash allocation failed\n");
        exit(2);
    }
    for (uint32_t insert_i = 0; insert_i < items; ++insert_i) {
        Entry *e = (Entry *)alloc_record(sizeof(*e));
        if (!e) {
            fprintf(stderr, "entry allocation failed\n");
            exit(2);
        }
        e->key = next_random();
        e->value = insert_i;
        size_t bucket = checked((size_t)(e->key % HASH_SLOTS), HASH_SLOTS,
                                "bucket");
        e->next = buckets[bucket];
        buckets[bucket] = e;
        entries[insert_i] = e;
    }

    uint64_t checksum = 0x84222325cbf29ce4ULL;
    for (uint32_t lookup_i = 0; lookup_i < items; ++lookup_i) {
        Entry *e = entries[lookup_i];
        size_t bucket = checked((size_t)(e->key % HASH_SLOTS), HASH_SLOTS,
                                "lookup");
        Entry *cursor = buckets[bucket];
        while (cursor && cursor->key != e->key)
            cursor = cursor->next;
        if (cursor)
            checksum = mix(checksum, (uint64_t)checked(cursor->value, items,
                                                       "value"));
    }
    for (uint32_t release_i = 0; release_i < items; ++release_i)
        release_record(entries[release_i]);
    free(entries);
    free(buckets);
    return checksum;
}

static int compare_uint64(const void *lhs, const void *rhs) {
    uint64_t a = *(const uint64_t *)lhs;
    uint64_t b = *(const uint64_t *)rhs;
    return (a > b) - (a < b);
}

/* Phase 3: sort and verify a deterministic array. */
static uint64_t phase_sort(uint32_t scale) {
    const size_t count = SORT_ITEMS_PER_SCALE * (size_t)scale;
    uint64_t *values = (uint64_t *)malloc(count * sizeof(*values));
    if (!values) {
        fprintf(stderr, "sort allocation failed\n");
        exit(2);
    }
    for (size_t fill_i = 0; fill_i < count; ++fill_i)
        values[checked(fill_i, count, "values")] = next_random();
    qsort(values, count, sizeof(*values), compare_uint64);
    uint64_t checksum = 0x27d4eb2f165667c5ULL;
    for (size_t verify_i = 0; verify_i < count; ++verify_i) {
        if (verify_i > 0 &&
            values[checked(verify_i - 1u, count, "prev")] >
                values[checked(verify_i, count, "cur")]) {
            fprintf(stderr, "sort order invariant violated\n");
            exit(2);
        }
        checksum = mix(checksum, values[checked(verify_i, count, "cur")]);
    }
    free(values);
    return checksum;
}

/* Phase 4: bump allocations with reset cycles. */
static uint64_t phase_arena(uint32_t scale) {
    const uint32_t rounds = ARENA_ROUNDS_PER_SCALE * scale;
    const size_t capacity = ARENA_BLOCK_BYTES;
    unsigned char *block = (unsigned char *)malloc(capacity);
    if (!block) {
        fprintf(stderr, "arena allocation failed\n");
        exit(2);
    }
    uint64_t checksum = 0x517cc1b727220a95ULL;
    size_t top = 0;
    for (uint32_t round = 0; round < rounds; ++round) {
        while (top + ARENA_RECORD_BYTES <= capacity) {
            unsigned char *record = block + checked(top, capacity, "arena");
            memset(record, (int)(next_random() & 0xffu), ARENA_RECORD_BYTES);
            checksum = mix(checksum, record[checked(0u, ARENA_RECORD_BYTES,
                                                   "record")]);
            top += ARENA_RECORD_BYTES;
        }
        top = 0; /* rewind: outstanding pointers are deliberately forgotten */
    }
    free(block);
    return checksum;
}

#define REPORT_PHASE(name, scale, checksum, start)                             \
    do {                                                                       \
        uint64_t elapsed = now_ns() - (start);                                 \
        printf("PHASE %s %u %" PRIu64 " %" PRIu64 "\n", name, (scale),          \
               elapsed, (checksum));                                           \
    } while (0)

int main(void) {
    const char *scale_text = getenv("CPLUS_BENCH_SCALE");
    uint32_t scale = scale_text ? (uint32_t)strtoul(scale_text, NULL, 10)
                                : SCALE_DEFAULT;
    if (scale == 0u) {
        fprintf(stderr, "CPLUS_BENCH_SCALE must be positive\n");
        return 2;
    }

    uint64_t begin = now_ns();
    uint64_t phase_start = now_ns();
    uint64_t matrix = phase_matrix(scale);
    REPORT_PHASE("matrix", scale, matrix, phase_start);

    phase_start = now_ns();
    uint64_t hash = phase_hash(scale);
    REPORT_PHASE("hash", scale, hash, phase_start);

    phase_start = now_ns();
    uint64_t sort = phase_sort(scale);
    REPORT_PHASE("sort", scale, sort, phase_start);

    phase_start = now_ns();
    uint64_t arena = phase_arena(scale);
    REPORT_PHASE("arena", scale, arena, phase_start);

    printf("TOTAL %u %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           "\n", scale, now_ns() - begin, matrix, hash, sort, arena);
    return 0;
}
