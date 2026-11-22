/* scenario_03.c -- double free through independent aliases.
 *
 * TEST 3: one allocation, N independent pointer variables that all alias the
 * same block, and a deterministic permutation of those aliases handed to the
 * ordinary free() spelling.  Because stdlib/safe/safe_c.h is included first,
 * malloc()/free() resolve to the tracked malloc_s()/free_s(): the FIRST free
 * is the single valid release and every later alias free must be refused
 * without touching the allocator or the host heap.
 *
 * Design notes / honesty limits:
 *  - The recent-free cache is bounded (1024 slots) and indexed by a hash of
 *    the pointer.  All aliases here share one address, so they share one
 *    cache slot; was_freed() is checked immediately after the valid free and
 *    before any reallocation of that address.
 *  - A persistent tracked "canary" block is written with a known pattern and
 *    re-verified throughout.  If a refused free ever reached the host heap,
 *    the allocator metadata would be corrupted and the canary/Live-count
 *    checks would catch it (the process is also expected to survive).
 *  - Host pointers are obtained with genuine host malloc() (via a locally
 *    #undef'd macro) and passed to the mapped free() only to prove refusal;
 *    they are always released with the matching host free(), never with the
 *    wrong allocator.
 *
 * Standalone C99.  main() returns 0 only on full success.
 */
#include "stdlib/safe/safe_c.h"      /* maps malloc/free/mem* -> hardened wrappers */
#include "stdlib/safe/safe_runtime.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_CASES 1200
#define MAX_ALIASES 16
#define CANARY_SIZE 1024u

typedef struct {
    size_t size;          /* 1..512 bytes requested from ordinary malloc() */
    int alias_count;      /* 1..16 independent alias variables */
    unsigned free_order;  /* permutation seed; equals the case index => unique */
    int delay_cases;      /* 0..3 interposed state queries between refused frees */
} case_tuple;

static case_tuple g_cases[NUM_CASES];
static volatile case_tuple g_current;
static volatile int g_case_index = -1;
static volatile int g_in_case = 0;
static volatile const char *g_phase = "init";
static volatile unsigned long g_violations = 0;
static volatile unsigned long g_findings = 0;
static size_t g_start_live = (size_t)-1;

static unsigned char *g_canary;
static const unsigned g_canary_seed = 0xC7u;

/* ---------------- async-signal-safe diagnostic helpers ---------------- */

static void write_all(int fd, const char *buf, size_t len) {
    while (len) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0) return;
        buf += n;
        len -= (size_t)n;
    }
}

static char *append_str(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

static char *append_ulong(char *p, unsigned long v) {
    char tmp[32];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char)('0' + (unsigned)(v % 10u));
        v /= 10u;
    }
    while (n) *p++ = tmp[--n];
    return p;
}

/* Emit a FINDING line carrying the exact failing tuple. */
static void print_finding(const char *msg) {
    char buf[640];
    char *p = buf;
    p = append_str(p, "FINDING: case=");
    p = append_ulong(p, (unsigned long)g_case_index);
    p = append_str(p, " tuple=(size=");
    p = append_ulong(p, (unsigned long)g_current.size);
    p = append_str(p, ", alias_count=");
    p = append_ulong(p, (unsigned long)g_current.alias_count);
    p = append_str(p, ", free_order=");
    p = append_ulong(p, (unsigned long)g_current.free_order);
    p = append_str(p, ", delay_cases=");
    p = append_ulong(p, (unsigned long)g_current.delay_cases);
    p = append_str(p, ") phase=");
    p = append_str(p, (const char *)g_phase);
    p = append_str(p, " msg=");
    p = append_str(p, msg ? msg : "");
    *p++ = '\n';
    write_all(2, buf, (size_t)(p - buf));
}

static void fail_now(void) {
    print_finding("aborting");
    _exit(70);
}

#define CHECK(cond, msg)                 \
    do {                                 \
        if (!(cond)) {                   \
            g_findings++;                \
            g_phase = "CHECK:" msg;      \
            fail_now();                  \
        }                                \
    } while (0)

/* Signal handler: report the exact in-flight tuple on any hard crash. */
static void crash_handler(int sig) {
    (void)sig;
    g_phase = "signal";
    print_finding("process crashed (signal)");
    _exit(70);
}

/* UBSan / runtime reports must be surfaced, never silently ignored. */
static void runtime_handler(const char *message) {
    g_violations++;
    g_phase = "runtime-handler";
    print_finding("runtime violation");
    if (message) write_all(2, message, strlen(message));
    write_all(2, "\n", 1);
}

/* ---------------- deterministic case table & helpers ---------------- */

static void build_cases(void) {
    unsigned x = 0xA5A5C123u;
    int i;
    for (i = 0; i < NUM_CASES; ++i) {
        x = x * 1664525u + 1013904223u;                  /* deterministic LCG */
        g_cases[i].size = (size_t)(1u + (x % 512u));      /* 1..512 */
        g_cases[i].alias_count = 1 + (int)((x >> 10) & 15u); /* 1..16 */
        g_cases[i].delay_cases = (int)((x >> 16) & 3u);    /* 0..3 */
        g_cases[i].free_order = (unsigned)i;              /* unique seed per tuple */
    }
}

/* Fisher-Yates permutation of [0,n) derived from the tuple's free_order. */
static void make_permutation(unsigned seed, int n, int *out) {
    unsigned x = seed * 2654435761u + 0x9E3779B9u;
    int i;
    for (i = 0; i < n; ++i) out[i] = i;
    for (i = n - 1; i > 0; --i) {
        int j, t;
        x = x * 1664525u + 1013904223u;
        j = (int)((x >> 8) % (unsigned)(i + 1));
        t = out[i]; out[i] = out[j]; out[j] = t;
    }
}

static unsigned char pattern_byte(size_t i, unsigned seed) {
    return (unsigned char)((i * 31u + seed * 17u + 0x5Au) & 0xFFu);
}

static void fill_pattern(unsigned char *p, size_t n, unsigned seed) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = pattern_byte(i, seed);
}

static int check_pattern(const unsigned char *p, size_t n, unsigned seed) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != pattern_byte(i, seed)) return 0;
    return 1;
}

/* Genuine host allocator access. safe_c.h maps malloc/free to the tracked
 * variants, so lift the macro only around these two calls; the host pointer
 * is later released by host_free(), never by the mapped free(). */
static void *host_malloc(size_t n) {
#undef malloc
    void *p = malloc(n);
#define malloc(sz) malloc_s(sz)
    return p;
}

static void host_free(void *p) {
#undef free
    free(p);
#define free(p) free_s(p)
}

/* ---------------- the scenario ---------------- */

static void run_case(int index) {
    case_tuple t = g_cases[index];
    unsigned seed = (unsigned)index * 2246822519u + 0x9E3779B9u;
    void *p;
    unsigned char *bytes;
    void *aliases[MAX_ALIASES];
    int perm[MAX_ALIASES];
    unsigned long long g0;
    cplus_safe_alloc_meta meta;
    size_t base = cplus_safe_alloc_live_count();
    size_t base_bytes = cplus_safe_alloc_live_bytes();
    size_t nbytes;
    int i, k;

    g_current = t;
    g_in_case = 1;
    g_phase = "alloc";

    p = malloc(t.size);                       /* -> malloc_s(): tracked */
    CHECK(p != NULL, "malloc returned NULL");
    CHECK(cplus_safe_alloc_live_count() == base + 1, "live count not +1 after malloc");
    nbytes = cplus_safe_ptr_size(p);
    CHECK(nbytes == (t.size ? t.size : 1u), "tracked size mismatch");

    g_phase = "meta";
    CHECK(cplus_safe_alloc_get_meta(p, &meta) == CPLUS_SAFE_OK, "get_meta failed");
    CHECK(meta.is_live == 1, "meta not live");
    CHECK(meta.byte_size == nbytes, "meta byte_size mismatch");
    g0 = meta.generation;
    CHECK(g0 != 0, "generation is zero");

    g_phase = "fill";
    bytes = (unsigned char *)p;
    fill_pattern(bytes, nbytes, seed);
    CHECK(check_pattern(bytes, nbytes, seed), "pattern fill failed");

    /* Build the independent aliases and validate each one pre-free. */
    g_phase = "aliases";
    for (i = 0; i < t.alias_count; ++i) {
        aliases[i] = p;
        CHECK(aliases[i] == p, "alias setup");
        CHECK(cplus_safe_ptr_is_live(aliases[i]) == 1, "alias not live before free");
        CHECK(cplus_safe_ptr_check_generation(aliases[i], g0) == 1,
              "alias generation mismatch before free");
        CHECK(cplus_safe_ptr_check_generation(aliases[i], g0 + 1) == 0,
              "alias matched wrong generation before free");
        CHECK(cplus_safe_ptr_was_freed(aliases[i]) == 0,
              "alias reported was_freed before free");
    }
    CHECK(check_pattern(bytes, nbytes, seed), "data changed before free");
    CHECK(check_pattern(g_canary, (size_t)CANARY_SIZE, g_canary_seed),
          "canary corrupted before free");

    /* Optional: a premature free of an interior alias must be refused and
     * must leave the original data completely intact. */
    if ((t.free_order & 2u) != 0u) {
        void *interior = (void *)((unsigned char *)p + 1);
        g_phase = "interior-free-refused";
        if (!cplus_safe_ptr_is_live(interior)) {
            errno = 0;
            free(interior);                   /* mapped free(): refused */
            CHECK(errno == EINVAL, "interior free not refused");
            CHECK(cplus_safe_ptr_is_live(p) == 1, "block lost after refused interior free");
            CHECK(cplus_safe_alloc_live_count() == base + 1,
                  "live count changed after refused interior free");
            CHECK(check_pattern(bytes, nbytes, seed),
                  "data changed after refused interior free");
        }
    }

    g_phase = "perm";
    make_permutation(t.free_order, t.alias_count, perm);

    g_phase = "free-loop";
    for (k = 0; k < t.alias_count; ++k) {
        int a = perm[k];
        void *alias = aliases[a];
        CHECK(alias == p, "permuted alias mismatch");

        if (k == 0) {
            /* The first attempted free is the single valid release. */
            g_phase = "valid-free";
            CHECK(check_pattern(bytes, nbytes, seed), "data changed just before valid free");
            free(alias);                      /* mapped free(): the one valid free */

            g_phase = "post-valid-free";
            CHECK(cplus_safe_alloc_live_count() == base,
                  "live count did not return to baseline after valid free");
            CHECK(cplus_safe_alloc_live_bytes() == base_bytes,
                  "live bytes did not return to baseline after valid free");
            for (i = 0; i < t.alias_count; ++i) {
                CHECK(cplus_safe_ptr_is_live(aliases[i]) == 0,
                      "FALSE LIVE answer for freed alias");
                CHECK(cplus_safe_ptr_was_freed(aliases[i]) == 1,
                      "was_freed false for freed alias");
                CHECK(cplus_safe_ptr_check_generation(aliases[i], g0) == 0,
                      "stale generation still matches after free");
                CHECK(cplus_safe_ptr_check_generation(aliases[i], g0 + 1) == 0,
                      "wrong generation matched after free");
            }
        } else {
            /* SECOND and later alias frees MUST be refused. */
            g_phase = "refused-free";
            errno = 0;
            free(alias);                      /* mapped free(): double free refused */
            CHECK(errno == EINVAL, "refused double free did not set errno=EINVAL");
            CHECK(cplus_safe_alloc_live_count() == base,
                  "live count changed after refused double free");
            CHECK(cplus_safe_alloc_live_bytes() == base_bytes,
                  "live bytes changed after refused double free");

            /* Interposed state queries (delay_cases) must not revive or
             * invalidate any alias observation. */
            g_phase = "delay-queries";
            for (i = 0; i < t.delay_cases; ++i) {
                (void)cplus_safe_ptr_is_live(alias);
                (void)cplus_safe_ptr_was_freed(alias);
                (void)cplus_safe_alloc_live_count();
            }

            g_phase = "post-refused";
            for (i = 0; i < t.alias_count; ++i) {
                CHECK(cplus_safe_ptr_is_live(aliases[i]) == 0,
                      "FALSE LIVE answer after refused double free");
                CHECK(cplus_safe_ptr_was_freed(aliases[i]) == 1,
                      "was_freed lost after refused double free");
            }
        }
        CHECK(check_pattern(g_canary, (size_t)CANARY_SIZE, g_canary_seed),
              "canary corrupted in free loop");
    }

    /* Host pointer passed to the mapped free(): must be refused, and the
     * host block must remain intact until released by the host allocator. */
    if ((t.free_order & 3u) == 0u) {
        size_t hs = 32u + (t.size & 31u);
        unsigned char *hb;
        void *host;
        g_phase = "host-invalid-free";
        host = host_malloc(hs);
        CHECK(host != NULL, "host malloc failed");
        CHECK(cplus_safe_ptr_is_live(host) == 0, "host pointer reported live");
        errno = 0;
        free(host);                           /* mapped free(): invalid free refused */
        CHECK(errno == EINVAL, "host pointer free was not refused");
        CHECK(free_checked_s(host) == CPLUS_SAFE_EINVAL,
              "free_checked_s did not refuse host pointer");
        CHECK(cplus_safe_alloc_live_count() == base,
              "host free changed tracked live count");
        hb = (unsigned char *)host;
        fill_pattern(hb, hs, 0x7Eu);
        CHECK(check_pattern(hb, hs, 0x7Eu),
              "host block corrupted by wrong-allocator free");
        host_free(host);                      /* correct allocator for host block */
    }

    /* Generation ABA: a fresh allocation must have a strictly newer
     * generation and stale aliases must not validate against it. */
    if ((t.free_order & 7u) == 5u) {
        void *fresh;
        cplus_safe_alloc_meta m2;
        g_phase = "generation-aba";
        fresh = malloc(nbytes + 4096u);
        CHECK(fresh != NULL, "fresh allocation failed");
        CHECK(cplus_safe_alloc_get_meta(fresh, &m2) == CPLUS_SAFE_OK,
              "fresh get_meta failed");
        CHECK(m2.generation > g0, "generation did not advance");
        if (fresh != p) {
            for (i = 0; i < t.alias_count; ++i) {
                CHECK(cplus_safe_ptr_check_generation(aliases[i], m2.generation) == 0,
                      "stale alias validated against fresh generation");
            }
        }
        free(fresh);
    }

    g_phase = "case-end";
    CHECK(check_pattern(g_canary, (size_t)CANARY_SIZE, g_canary_seed),
          "canary corrupted at case end");
    CHECK(cplus_safe_alloc_live_count() == base,
          "live count not baseline at case end");
    CHECK(cplus_safe_alloc_live_bytes() == base_bytes,
          "live bytes not baseline at case end");
    g_in_case = 0;
}

int main(void) {
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    cplus_runtime_set_violation_trap(0);
    cplus_runtime_set_handler(runtime_handler);
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS,  crash_handler);
    signal(SIGILL,  crash_handler);
    signal(SIGFPE,  crash_handler);

    g_start_live = cplus_safe_alloc_live_count();

    g_phase = "canary-alloc";
    g_canary = (unsigned char *)malloc((size_t)CANARY_SIZE);
    if (!g_canary) {
        write_all(2, "FINDING: phase=canary-alloc msg=malloc failed\n", 46);
        return 70;
    }
    fill_pattern(g_canary, (size_t)CANARY_SIZE, g_canary_seed);

    g_phase = "build-cases";
    build_cases();

    for (i = 0; i < NUM_CASES; ++i) {
        g_case_index = i;
        run_case(i);
    }

    g_phase = "final";
    CHECK(check_pattern(g_canary, (size_t)CANARY_SIZE, g_canary_seed),
          "canary corrupted at final check");
    free(g_canary);
    CHECK(cplus_safe_alloc_live_count() == g_start_live,
          "live count not back to program baseline");
    CHECK(g_violations == 0, "runtime violations were reported");
    CHECK(g_findings == 0, "findings were recorded");

    printf("scenario_03: %d cases ok\n", NUM_CASES);
    return 0;
}
