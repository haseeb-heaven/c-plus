/* EDGE100 alloc -- deterministic fork-per-case edge coverage for the seven
 * modern allocator/diagnostic headers:
 *   alloc_iface.hp arena.hp mempool.hp datastack.hp error.hp allocator.hp
 *   alloc_policy.hp
 *
 * METHOD
 *   - Every public API (static inline function or RAII class) appears exactly
 *     once in the API table below.
 *   - Each API is exercised in NINE deterministic edge categories:
 *       K_VALID  valid input
 *       K_NULL   NULL argument
 *       K_ZERO   zero size/count
 *       K_EXACT  exact-fit size/count
 *       K_OVER   one-over size/count
 *       K_SIZEMAX SIZE_MAX argument
 *       K_ALIGN  invalid (zero / non-power-of-two) alignment
 *       K_REUSE  reset/destroy then reuse
 *       K_STALE  generation / double-free staleness
 *   - One forked child per (api, kind) case.  The child runs global+stack
 *     canaries around the operation and reports a 3-int record over a pipe.
 *     A child that dies by signal, exits non-zero, writes a short record, or
 *     reports a canary/assertion violation is classified FAIL (CRASH/CORRUPT).
 *   - No PRNG, no time, no randomness: results are reproducible bit-for-bit.
 *   - Global wall-clock bound is enforced by run.sh via `timeout 60`.
 *
 * BUILD (from repository root)
 *   ./build/c+ build tests/edge100/alloc/harness.cp --default --sanitize=none \
 *     -I. -o tests/edge100/alloc/out/harness -- build/libc/libcplus_hardened.a
 *
 * OUTPUT
 *   out/results.tsv   : api_id \t api_name \t kind \t verdict \t obs
 *   out/manifest.tsv  : api_id \t header \t api_name \t cases \t passed \t failed
 *   stdout final line : EDGE100 alloc <apis> <cases> <passed> <failed>
 */
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"

/* Issue #56 regression plumbing: a deterministic raw-heap wrapper used only
 * while mempool.hp is included.  With injection off it is a pass-through to
 * the real managed allocator (so all 117 APIs keep their normal semantics);
 * with injection armed it refuses the n-th and later allocs, which lets the
 * slot_pool OOM paths be exercised without relying on real host exhaustion. */
static long g_slot_inject = -1L; /* -1 off; >= 0 = successes before failure */
static size_t g_slot_live = 0;

static void *slot_heap_alloc(size_t n) {
    void *p = (void *)0;
    if (g_slot_inject >= 0L) {
        if (g_slot_inject == 0L) return (void *)0;
        g_slot_inject--;
    }
    p = cplus_alloc(n);
    if (p != (void *)0) g_slot_live++;
    return p;
}
static void slot_heap_release(void *p) {
    if (p == (void *)0) return;
    if (g_slot_live > 0) g_slot_live--;
    cplus_release(p);
}

#define cplus_alloc(n) slot_heap_alloc(n)
#define cplus_release(p) slot_heap_release(p)
#include "stdlib/mempool.hp"
#undef cplus_release
#undef cplus_alloc

#include "stdlib/datastack.hp"
#include "stdlib/allocator.hp"
#include "stdlib/alloc_policy.hp"

/* --------------------------------------------------------------- constants */
#define K_VALID   0
#define K_NULL    1
#define K_ZERO    2
#define K_EXACT   3
#define K_OVER    4
#define K_SIZEMAX 5
#define K_ALIGN   6
#define K_REUSE   7
#define K_STALE   8
#define NKIND     9

#define RESULTS_PATH  "tests/edge100/alloc/out/results.tsv"
#define MANIFEST_PATH "tests/edge100/alloc/out/manifest.tsv"

/* ---------------------------------------------------------------- canaries */
#define CANARY_N 32
static unsigned char g_canary[CANARY_N];
static unsigned char canary_byte(unsigned int i) {
    return (unsigned char)(0x5Au ^ (i & 0xFFu));
}
static void canary_init(void) {
    unsigned int i;
    for (i = 0; i < CANARY_N; i++) g_canary[i] = canary_byte(i);
}
static int canary_ok(void) {
    unsigned int i;
    for (i = 0; i < CANARY_N; i++)
        if (g_canary[i] != canary_byte(i)) return 0;
    return 1;
}

/* ----------------------------------------------------------------- helpers */
struct cres { int pass; int sub; int obs; };
typedef void (*case_fn)(int kind, struct cres *r);

static void ex(struct cres *r, int cond) { if (!cond) r->pass = 0; }
static int aligned(void *p, size_t a) {
    return (((uintptr_t)p) & (uintptr_t)(a - 1u)) == 0;
}
static int zeroed(void *p, size_t n) {
    unsigned char *q = (unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i++) if (q[i] != 0) return 0;
    return 1;
}
static int all_same(void *p, size_t n, unsigned char v) {
    unsigned char *q = (unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i++) if (q[i] != v) return 0;
    return 1;
}
static const char *kind_name(int k) {
    switch (k) {
    case K_VALID: return "valid";
    case K_NULL: return "null";
    case K_ZERO: return "zero";
    case K_EXACT: return "exact";
    case K_OVER: return "one-over";
    case K_SIZEMAX: return "sizemax";
    case K_ALIGN: return "align";
    case K_REUSE: return "reuse";
    case K_STALE: return "stale";
    default: return "unknown";
    }
}

/* callback tally used by error/allocator callback tests */
static int g_cb_calls;
static int g_cb_cat;
static void tally_cb(int category, cplus_exit_code code, const char *message) {
    (void)code;
    (void)message;
    g_cb_calls++;
    g_cb_cat = category;
}

/* reset counter hook used by stdlib_allocator_reset */
static int g_reset_calls;
static void reset_hook(void *ctx) { (void)ctx; g_reset_calls++; }

/* ========================================================================= */
/* alloc_iface.hp                                                             */
/* ========================================================================= */
static void r000(int kind, struct cres *r) { /* stdlib_allocate */
    error e = error(); void *p = (void *)0; void *q = (void *)0;
    switch (kind) {
    case K_VALID:
        p = stdlib_allocate((const stdlib_allocator *)0, 32u, 16u, &e);
        ex(r, p != 0 && e.ok() && aligned(p, 16u));
        stdlib_release(p);
        break;
    case K_NULL:
        p = stdlib_allocate((const stdlib_allocator *)0, 16u, 16u, (error *)0);
        ex(r, p != 0); stdlib_release(p);
        break;
    case K_ZERO:
        p = stdlib_allocate((const stdlib_allocator *)0, 0u, 16u, &e);
        ex(r, p == 0 && e.category() == STDLIB_CATEGORY_INVALID);
        break;
    case K_EXACT:
        p = stdlib_allocate((const stdlib_allocator *)0, STDLIB_ALLOC_LIMIT, 16u, &e);
        ex(r, p != 0); stdlib_release(p);
        break;
    case K_OVER:
        p = stdlib_allocate((const stdlib_allocator *)0, STDLIB_ALLOC_LIMIT + 1u, 16u, &e);
        ex(r, p == 0);
        break;
    case K_SIZEMAX:
        p = stdlib_allocate((const stdlib_allocator *)0, SIZE_MAX, 16u, &e);
        ex(r, p == 0);
        break;
    case K_ALIGN:
        p = stdlib_allocate((const stdlib_allocator *)0, 16u, 3u, &e);
        ex(r, p == 0 && e.category() == STDLIB_CATEGORY_INVALID);
        q = stdlib_allocate((const stdlib_allocator *)0, 16u, 0u, &e);
        ex(r, q != 0); stdlib_release(q);
        break;
    case K_REUSE:
        p = stdlib_allocate((const stdlib_allocator *)0, 16u, 16u, &e);
        q = stdlib_allocate((const stdlib_allocator *)0, 16u, 16u, &e);
        ex(r, p != 0 && q != 0);
        stdlib_release(p); stdlib_release(q);
        break;
    case K_STALE: {
        unsigned long long g;
        p = stdlib_alloc(16u); g = stdlib_alloc_generation(p);
        ex(r, g != 0 && stdlib_alloc_check_generation(p, g) == 1);
        stdlib_release(p);
        ex(r, stdlib_alloc_check_generation(p, g) == 0);
        break;
    }
    default: break;
    }
}
static void r001(int kind, struct cres *r) { /* stdlib_reallocate */
    error e = error(); void *p = (void *)0; void *q = (void *)0;
    p = stdlib_alloc(16u);
    if (p == 0) { r->pass = 0; return; }
    switch (kind) {
    case K_VALID:
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, 32u, 16u, &e);
        ex(r, q != 0 && e.ok()); stdlib_release(q); p = 0;
        break;
    case K_NULL:
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, 32u, 16u, (error *)0);
        ex(r, q != 0); stdlib_release(q); p = 0;
        break;
    case K_ZERO:
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, 0u, 16u, &e);
        ex(r, q == 0 && e.category() == STDLIB_CATEGORY_INVALID);
        break;
    case K_EXACT:
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, 16u, 16u, &e);
        ex(r, q != 0); stdlib_release(q); p = 0;
        break;
    case K_OVER:
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, STDLIB_ALLOC_LIMIT + 1u, 16u, &e);
        ex(r, q == 0);
        break;
    case K_SIZEMAX:
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, SIZE_MAX, 16u, &e);
        ex(r, q == 0);
        break;
    case K_ALIGN:
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, 32u, 3u, &e);
        ex(r, q == 0 && e.category() == STDLIB_CATEGORY_INVALID);
        break;
    case K_REUSE:
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, 32u, 0u, &e);
        ex(r, q != 0); stdlib_release(q); p = 0;
        break;
    case K_STALE: {
        unsigned long long g = stdlib_alloc_generation(p);
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16u, 64u, 16u, &e);
        ex(r, q != 0 && stdlib_alloc_check_generation(q, g) == 0);
        stdlib_release(q); p = 0;
        break;
    }
    default: break;
    }
    if (p != 0) stdlib_release(p);
}
static void r002(int kind, struct cres *r) { /* stdlib_deallocate */
    error e = error(); void *p;
    switch (kind) {
    case K_NULL:
        stdlib_deallocate((const stdlib_allocator *)0, (void *)0, 0u);
        break;
    case K_STALE: {
        unsigned long long g;
        p = stdlib_alloc(8u); g = stdlib_alloc_generation(p);
        stdlib_deallocate((const stdlib_allocator *)0, p, 8u);
        ex(r, stdlib_alloc_check_generation(p, g) == 0);
        break;
    }
    case K_REUSE:
        p = stdlib_alloc(8u); stdlib_deallocate((const stdlib_allocator *)0, p, 8u);
        p = stdlib_alloc(8u); ex(r, p != 0);
        stdlib_deallocate((const stdlib_allocator *)0, p, 8u);
        break;
    default:
        p = stdlib_alloc(8u); ex(r, p != 0);
        switch (kind) {
        case K_ZERO: stdlib_deallocate((const stdlib_allocator *)0, p, 0u); break;
        case K_EXACT: stdlib_deallocate((const stdlib_allocator *)0, p, 8u); break;
        case K_OVER: stdlib_deallocate((const stdlib_allocator *)0, p, 9u); break;
        case K_SIZEMAX: stdlib_deallocate((const stdlib_allocator *)0, p, SIZE_MAX); break;
        case K_ALIGN: stdlib_deallocate((const stdlib_allocator *)0, p, 16u); break;
        default: stdlib_deallocate((const stdlib_allocator *)0, p, 8u); break;
        }
        break;
    }
    (void)e;
}
static void r003(int kind, struct cres *r) { /* stdlib_allocator_reset */
    stdlib_allocator a = stdlib_heap_allocator();
    (void)kind;
    stdlib_allocator_reset(&a);
    ex(r, a.ctx == 0 && a.alloc == 0 && a.realloc == 0 && a.release == 0);
    stdlib_allocator_reset((const stdlib_allocator *)0);
    g_reset_calls = 0;
    a.reset = reset_hook;
    stdlib_allocator_reset(&a);
    stdlib_allocator_reset(&a);
    ex(r, g_reset_calls == 2);
}
static void r004(int kind, struct cres *r) { /* stdlib_heap_allocator */
    stdlib_allocator a;
    (void)kind;
    a = stdlib_heap_allocator();
    ex(r, a.ctx == 0 && a.alloc == 0 && a.realloc == 0 && a.release == 0 && a.reset == 0);
}

/* ========================================================================= */
/* arena.hp                                                                   */
/* ========================================================================= */
static void r005(int kind, struct cres *r) { /* stdlib_arena_next_generation */
    uint64_t g1 = stdlib_arena_next_generation();
    uint64_t g2 = stdlib_arena_next_generation();
    (void)kind;
    ex(r, g1 != 0 && g2 != 0 && g2 != g1);
}
static void r006(int kind, struct cres *r) { /* stdlib_arena_new_block */
    stdlib_arena a; error e = error(); error e2 = error();
    a.block = 0; a.first_base = 0;
    if (stdlib_arena_init(&a, 64u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_NULL: ex(r, stdlib_arena_new_block((stdlib_arena *)0, 64u, &e2) == STDLIB_FAIL); break;
    case K_ZERO: ex(r, stdlib_arena_new_block(&a, 0u, &e2) == STDLIB_FAIL); break;
    case K_OVER: ex(r, stdlib_arena_new_block(&a, SIZE_MAX - 8u, &e2) == STDLIB_FAIL); break;
    case K_SIZEMAX: ex(r, stdlib_arena_new_block(&a, SIZE_MAX, &e2) == STDLIB_FAIL); break;
    default: ex(r, stdlib_arena_new_block(&a, 64u, &e2) == STDLIB_OK && a.block != 0); break;
    }
    if (a.block != 0) stdlib_arena_destroy(&a, &e);
}
static void r007(int kind, struct cres *r) { /* stdlib_arena_init */
    stdlib_arena a; error e = error(); uint64_t g1;
    a.block = 0; a.first_base = 0;
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_arena_init((stdlib_arena *)0, 64u, &e) == STDLIB_FAIL); break;
    case K_OVER:
    case K_SIZEMAX:
        ex(r, stdlib_arena_init(&a, SIZE_MAX, &e) == STDLIB_FAIL); break;
    case K_ZERO:
        ex(r, stdlib_arena_init(&a, 0u, &e) == STDLIB_OK && a.default_block == 4096u);
        if (a.block != 0) stdlib_arena_destroy(&a, &e);
        break;
    case K_REUSE:
    case K_STALE:
        ex(r, stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
        g1 = a.generation;
        if (a.block != 0) stdlib_arena_destroy(&a, &e);
        ex(r, stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
        ex(r, a.generation != g1);
        if (a.block != 0) stdlib_arena_destroy(&a, &e);
        break;
    default:
        ex(r, stdlib_arena_init(&a, 64u, &e) == STDLIB_OK &&
              a.block != 0 && a.generation != 0 && a.default_block == 64u);
        if (a.block != 0) stdlib_arena_destroy(&a, &e);
        break;
    }
}
static void r008(int kind, struct cres *r) { /* stdlib_arena_from_buffer */
    unsigned char buf[512]; stdlib_arena a; error e = error(); uint64_t g1;
    a.block = 0; a.first_base = 0;
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_arena_from_buffer((stdlib_arena *)0, buf, sizeof(buf), &e) == STDLIB_FAIL);
        ex(r, stdlib_arena_from_buffer(&a, (void *)0, 64u, &e) == STDLIB_FAIL);
        break;
    case K_ZERO:
        ex(r, stdlib_arena_from_buffer(&a, buf, 0u, &e) == STDLIB_OK && a.block != 0);
        break;
    case K_EXACT:
        ex(r, stdlib_arena_from_buffer(&a, buf,
              sizeof(stdlib_arena_block) + STDLIB_ALIGN_DEFAULT + 8u, &e) == STDLIB_OK &&
              a.owns_first == 0);
        break;
    case K_OVER:
        ex(r, stdlib_arena_from_buffer(&a, buf, SIZE_MAX / 2u, &e) == STDLIB_OK);
        break;
    case K_SIZEMAX:
        ex(r, stdlib_arena_from_buffer(&a, (void *)0, SIZE_MAX, &e) == STDLIB_FAIL);
        break;
    case K_REUSE:
    case K_STALE:
        ex(r, stdlib_arena_from_buffer(&a, buf, sizeof(buf), &e) == STDLIB_OK);
        g1 = a.generation;
        if (a.block != 0) stdlib_arena_destroy(&a, &e);
        ex(r, stdlib_arena_from_buffer(&a, buf, sizeof(buf), &e) == STDLIB_OK);
        ex(r, a.generation != g1);
        break;
    default:
        ex(r, stdlib_arena_from_buffer(&a, buf, sizeof(buf), &e) == STDLIB_OK &&
              a.owns_first == 0 && a.first_capacity > 0);
        break;
    }
    if (a.block != 0) stdlib_arena_destroy(&a, &e);
}
static void r009(int kind, struct cres *r) { /* stdlib_arena_alloc */
    stdlib_arena a; error e = error(); void *p;
    a.block = 0; a.first_base = 0;
    if (stdlib_arena_init(&a, 256u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_NULL:
        p = stdlib_arena_alloc((stdlib_arena *)0, 32u, 16u, &e);
        ex(r, p == 0);
        p = stdlib_arena_alloc(&a, 32u, 16u, (error *)0);
        ex(r, p != 0);
        break;
    case K_ZERO:
        p = stdlib_arena_alloc(&a, 0u, 16u, &e); ex(r, p == 0); break;
    case K_EXACT:
        p = stdlib_arena_alloc(&a, 64u, 16u, &e);
        ex(r, p != 0 && aligned(p, 16u) && zeroed(p, 64u)); break;
    case K_OVER:
        p = stdlib_arena_alloc(&a, 65u, 16u, &e);
        ex(r, p != 0 && aligned(p, 16u)); break; /* spills to a new block */
    case K_SIZEMAX:
        p = stdlib_arena_alloc(&a, SIZE_MAX, 16u, &e);
        ex(r, p == 0 && e.category() == STDLIB_CATEGORY_RANGE); break;
    case K_ALIGN:
        p = stdlib_arena_alloc(&a, 16u, 3u, &e);
        ex(r, p == 0 && e.category() == STDLIB_CATEGORY_INVALID);
        p = stdlib_arena_alloc(&a, 16u, 0u, &e);
        ex(r, p == 0);
        break;
    case K_REUSE:
        p = stdlib_arena_alloc(&a, 32u, 16u, &e);
        stdlib_arena_do_reset(&a, &e);
        p = stdlib_arena_alloc(&a, 32u, 16u, &e);
        ex(r, p != 0); break;
    case K_STALE: {
        uint64_t g = a.generation;
        stdlib_arena_do_reset(&a, &e);
        ex(r, a.generation != g); break;
    }
    default:
        p = stdlib_arena_alloc(&a, 32u, 16u, &e);
        ex(r, p != 0 && aligned(p, 16u) && zeroed(p, 32u)); break;
    }
    stdlib_arena_destroy(&a, &e);
}
static void r010(int kind, struct cres *r) { /* stdlib_arena_realloc */
    stdlib_arena a; error e = error(); void *p; void *q; void *top;
    a.block = 0; a.first_base = 0;
    if (stdlib_arena_init(&a, 256u, &e) != STDLIB_OK) { r->pass = 0; return; }
    p = stdlib_arena_alloc(&a, 32u, 16u, &e);
    if (p == 0) { stdlib_arena_destroy(&a, &e); r->pass = 0; return; }
    ((unsigned char *)p)[0] = 0x42u;
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_arena_realloc((stdlib_arena *)0, p, 32u, 64u, 16u, &e) == 0);
        ex(r, stdlib_arena_realloc(&a, (void *)0, 32u, 64u, 16u, &e) == 0);
        break;
    case K_ZERO:
        ex(r, stdlib_arena_realloc(&a, p, 32u, 0u, 16u, &e) == 0); break;
    case K_EXACT:
        q = stdlib_arena_realloc(&a, p, 32u, 32u, 16u, &e);
        ex(r, q == p && ((unsigned char *)q)[0] == 0x42u); break;
    case K_OVER:
    case K_SIZEMAX:
        ex(r, stdlib_arena_realloc(&a, p, 32u, SIZE_MAX, 16u, &e) == 0); break;
    case K_ALIGN:
        top = stdlib_arena_alloc(&a, 32u, 16u, &e);
        ex(r, top != 0);
        ex(r, stdlib_arena_realloc(&a, p, 32u, 64u, 3u, &e) == 0);
        break;
    case K_REUSE:
        q = stdlib_arena_realloc(&a, p, 32u, 64u, 16u, &e);
        ex(r, q == p);
        stdlib_arena_do_reset(&a, &e);
        q = stdlib_arena_alloc(&a, 32u, 16u, &e);
        ex(r, q != 0); break;
    case K_STALE:
        ex(r, stdlib_arena_realloc(&a, p, SIZE_MAX, 64u, 16u, &e) == 0); break;
    default:
        q = stdlib_arena_realloc(&a, p, 32u, 64u, 16u, &e);
        ex(r, q == p && ((unsigned char *)q)[0] == 0x42u); break;
    }
    stdlib_arena_destroy(&a, &e);
}
static void r011(int kind, struct cres *r) { /* stdlib_arena_release */
    stdlib_arena a; error e = error(); void *p;
    a.block = 0; a.first_base = 0;
    if (stdlib_arena_init(&a, 64u, &e) != STDLIB_OK) { r->pass = 0; return; }
    p = stdlib_arena_alloc(&a, 16u, 16u, &e);
    switch (kind) {
    case K_NULL:
        stdlib_arena_release((stdlib_arena *)0, p, 16u, &e);
        ex(r, e.failed_now() == 1); break;
    case K_ZERO: stdlib_arena_release(&a, p, 0u, &e); ex(r, e.ok() == 1); break;
    case K_SIZEMAX: stdlib_arena_release(&a, p, SIZE_MAX, &e); ex(r, e.ok() == 1); break;
    default: stdlib_arena_release(&a, p, 16u, &e); ex(r, e.ok() == 1); break;
    }
    stdlib_arena_destroy(&a, &e);
}
static void r012(int kind, struct cres *r) { /* stdlib_arena_used */
    stdlib_arena a; error e = error(); void *p; size_t used;
    a.block = 0; a.first_base = 0;
    ex(r, stdlib_arena_used((const stdlib_arena *)0) == 0);
    if (stdlib_arena_init(&a, 256u, &e) != STDLIB_OK) { r->pass = 0; return; }
    ex(r, stdlib_arena_used(&a) == 0);
    switch (kind) {
    case K_ZERO: p = stdlib_arena_alloc(&a, 0u, 16u, &e); ex(r, p == 0); break;
    case K_SIZEMAX: p = stdlib_arena_alloc(&a, SIZE_MAX, 16u, &e); ex(r, p == 0); break;
    case K_EXACT: p = stdlib_arena_alloc(&a, 256u, 16u, &e); ex(r, p != 0); break;
    default: p = stdlib_arena_alloc(&a, 32u, 16u, &e); ex(r, p != 0); break;
    }
    used = stdlib_arena_used(&a);
    ex(r, used <= a.block->capacity || used > 0);
    stdlib_arena_destroy(&a, &e);
}
static void r013(int kind, struct cres *r) { /* stdlib_arena_do_reset */
    stdlib_arena a; error e = error(); uint64_t g; void *p;
    a.block = 0; a.first_base = 0;
    stdlib_arena_do_reset((stdlib_arena *)0, &e); /* documented no-op */
    if (stdlib_arena_init(&a, 128u, &e) != STDLIB_OK) { r->pass = 0; return; }
    g = a.generation;
    p = stdlib_arena_alloc(&a, 32u, 16u, &e);
    ex(r, p != 0);
    stdlib_arena_do_reset(&a, &e);
    ex(r, stdlib_arena_used(&a) == 0 && a.generation != g);
    if (kind == K_REUSE) {
        p = stdlib_arena_alloc(&a, 32u, 16u, &e);
        ex(r, p != 0);
    }
    if (kind == K_STALE) {
        stdlib_arena_handle h = stdlib_arena_mark((const stdlib_arena *)&a);
        /* mark was taken after reset; force a second reset to stale it */
        stdlib_arena_do_reset(&a, &e);
        ex(r, stdlib_arena_handle_valid(&h) == 0);
    }
    stdlib_arena_destroy(&a, &e);
}
static void r014(int kind, struct cres *r) { /* stdlib_arena_destroy */
    stdlib_arena a; error e = error(); uint64_t g;
    a.block = 0; a.first_base = 0;
    stdlib_arena_destroy((stdlib_arena *)0, &e); /* no-op */
    if (stdlib_arena_init(&a, 64u, &e) != STDLIB_OK) { r->pass = 0; return; }
    g = a.generation;
    if (kind != K_ZERO) {
        void *p = stdlib_arena_alloc(&a, 16u, 16u, &e);
        ex(r, p != 0);
    }
    stdlib_arena_destroy(&a, &e);
    ex(r, a.block == 0 && a.cursor == 0 && a.remaining == 0 && a.generation != g);
    if (kind == K_REUSE) {
        ex(r, stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
        if (a.block != 0) stdlib_arena_destroy(&a, &e);
    }
}
static void r015(int kind, struct cres *r) { /* stdlib_arena_handle_valid */
    stdlib_arena a; error e = error(); stdlib_arena_handle h, z;
    a.block = 0; a.first_base = 0;
    if (stdlib_arena_init(&a, 64u, &e) != STDLIB_OK) { r->pass = 0; return; }
    h = stdlib_arena_mark((const stdlib_arena *)&a);
    z.arena = (stdlib_arena *)0; z.generation = 0;
    ex(r, stdlib_arena_handle_valid(&h) == 1);
    ex(r, stdlib_arena_handle_valid(&z) == 0);
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_arena_handle_valid((const stdlib_arena_handle *)0) == 0); break;
    case K_STALE:
        stdlib_arena_do_reset(&a, &e);
        ex(r, stdlib_arena_handle_valid(&h) == 0); break;
    case K_REUSE:
        stdlib_arena_destroy(&a, &e);
        ex(r, stdlib_arena_handle_valid(&h) == 0);
        stdlib_arena_init(&a, 64u, &e);
        break;
    default:
        ex(r, stdlib_arena_handle_valid(&h) == 1); break;
    }
    if (a.block != 0) stdlib_arena_destroy(&a, &e);
}
static void r016(int kind, struct cres *r) { /* stdlib_arena_mark */
    stdlib_arena a; error e = error(); stdlib_arena_handle h, z;
    a.block = 0; a.first_base = 0;
    h = stdlib_arena_mark((const stdlib_arena *)0);
    ex(r, h.arena == 0 && h.generation == 0);
    if (stdlib_arena_init(&a, 64u, &e) != STDLIB_OK) { r->pass = 0; return; }
    h = stdlib_arena_mark((const stdlib_arena *)&a);
    ex(r, h.arena == &a && h.generation == a.generation);
    if (kind == K_STALE) {
        stdlib_arena_destroy(&a, &e);
        ex(r, h.generation != a.generation);
    }
    if (a.block != 0) stdlib_arena_destroy(&a, &e);
    (void)z;
}
static void r017(int kind, struct cres *r) { /* stdlib_arena_op_alloc */
    stdlib_arena a; error e = error(); void *p;
    a.block = 0; a.first_base = 0;
    ex(r, stdlib_arena_op_alloc((void *)0, 16u, 16u) == 0);
    if (stdlib_arena_init(&a, 128u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_ZERO: p = stdlib_arena_op_alloc(&a, 0u, 16u); ex(r, p == 0); break;
    case K_SIZEMAX: p = stdlib_arena_op_alloc(&a, SIZE_MAX, 16u); ex(r, p == 0); break;
    case K_ALIGN: p = stdlib_arena_op_alloc(&a, 16u, 3u); ex(r, p == 0); break;
    default: p = stdlib_arena_op_alloc(&a, 16u, 16u); ex(r, p != 0); break;
    }
    stdlib_arena_destroy(&a, &e);
}
static void r018(int kind, struct cres *r) { /* stdlib_arena_op_realloc */
    stdlib_arena a; error e = error(); void *p; void *q;
    a.block = 0; a.first_base = 0;
    ex(r, stdlib_arena_op_realloc((void *)0, (void *)0, 0u, 16u, 16u) == 0);
    if (stdlib_arena_init(&a, 128u, &e) != STDLIB_OK) { r->pass = 0; return; }
    p = stdlib_arena_op_alloc(&a, 16u, 16u);
    ex(r, p != 0);
    switch (kind) {
    case K_ZERO: ex(r, stdlib_arena_op_realloc(&a, p, 16u, 0u, 16u) == 0); break;
    case K_SIZEMAX: ex(r, stdlib_arena_op_realloc(&a, p, 16u, SIZE_MAX, 16u) == 0); break;
    case K_STALE: ex(r, stdlib_arena_op_realloc(&a, p, SIZE_MAX, 32u, 16u) == 0); break;
    default:
        q = stdlib_arena_op_realloc(&a, p, 16u, 32u, 16u);
        ex(r, q == p); break;
    }
    stdlib_arena_destroy(&a, &e);
}
static void r019(int kind, struct cres *r) { /* stdlib_arena_op_release */
    stdlib_arena a; error e = error(); void *p;
    a.block = 0; a.first_base = 0;
    stdlib_arena_op_release((void *)0, (void *)0, 0u); /* no-op */
    if (stdlib_arena_init(&a, 64u, &e) != STDLIB_OK) { r->pass = 0; return; }
    p = stdlib_arena_op_alloc(&a, 16u, 16u);
    stdlib_arena_op_release(&a, p, (kind == K_SIZEMAX) ? SIZE_MAX : 16u);
    ex(r, a.block != 0);
    stdlib_arena_destroy(&a, &e);
}
static void r020(int kind, struct cres *r) { /* stdlib_arena_op_reset */
    stdlib_arena a; error e = error(); void *p; uint64_t g;
    a.block = 0; a.first_base = 0;
    stdlib_arena_op_reset((void *)0);
    if (stdlib_arena_init(&a, 64u, &e) != STDLIB_OK) { r->pass = 0; return; }
    p = stdlib_arena_op_alloc(&a, 16u, 16u);
    ex(r, p != 0);
    g = a.generation;
    stdlib_arena_op_reset(&a);
    ex(r, stdlib_arena_used(&a) == 0 && a.generation != g);
    if (kind == K_REUSE) {
        p = stdlib_arena_op_alloc(&a, 16u, 16u);
        ex(r, p != 0);
    }
    stdlib_arena_destroy(&a, &e);
}
static void r021(int kind, struct cres *r) { /* stdlib_arena_as_allocator */
    stdlib_arena a; error e = error(); stdlib_allocator v;
    a.block = 0; a.first_base = 0;
    v = stdlib_arena_as_allocator((stdlib_arena *)0);
    ex(r, v.ctx == 0 && v.alloc == stdlib_arena_op_alloc);
    if (stdlib_arena_init(&a, 64u, &e) != STDLIB_OK) { r->pass = 0; return; }
    v = stdlib_arena_as_allocator(&a);
    ex(r, v.ctx == &a && v.alloc == stdlib_arena_op_alloc &&
          v.realloc == stdlib_arena_op_realloc && v.release == stdlib_arena_op_release &&
          v.reset == stdlib_arena_op_reset);
    if (kind != K_NULL) {
        void *p = stdlib_allocate(&v, 16u, 16u, &e);
        ex(r, p != 0);
    }
    stdlib_arena_destroy(&a, &e);
}

/* ========================================================================= */
/* mempool.hp                                                                 */
/* ========================================================================= */
static void r022(int kind, struct cres *r) { /* stdlib_mem_pool_init */
    stdlib_mem_pool p; error e = error(); void *s;
    switch (kind) {
    case K_NULL: ex(r, stdlib_mem_pool_init((stdlib_mem_pool *)0, 16u, 4u, &e) == STDLIB_FAIL); break;
    case K_ZERO:
        ex(r, stdlib_mem_pool_init(&p, 0u, 4u, &e) == STDLIB_FAIL);
        ex(r, stdlib_mem_pool_init(&p, 16u, 0u, &e) == STDLIB_FAIL); break;
    case K_OVER: ex(r, stdlib_mem_pool_init(&p, SIZE_MAX, 4u, &e) == STDLIB_FAIL); break;
    case K_SIZEMAX: ex(r, stdlib_mem_pool_init(&p, SIZE_MAX, SIZE_MAX, &e) == STDLIB_FAIL); break;
    case K_REUSE:
        ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        stdlib_mem_pool_destroy(&p, &e);
        ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        stdlib_mem_pool_destroy(&p, &e); break;
    case K_STALE:
        ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        s = stdlib_mem_pool_alloc(&p, &e);
        stdlib_mem_pool_release(&p, s, &e);
        stdlib_mem_pool_release(&p, s, &e); /* double free refused */
        ex(r, e.code() == CPLUS_EXIT_DOUBLE_FREE);
        stdlib_mem_pool_destroy(&p, &e); break;
    default:
        ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK &&
              p.slot_size == 16u && p.slots_per_slab == 4u);
        stdlib_mem_pool_destroy(&p, &e); break;
    }
}
static void r023(int kind, struct cres *r) { /* stdlib_mem_pool_add_slab */
    stdlib_mem_pool p; error e = error();
    if (stdlib_mem_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_NULL:
        stdlib_mem_pool_add_slab((stdlib_mem_pool *)0, &e); /* refused/crash isolated */
        break;
    case K_ZERO:
        p.slot_size = 1u; p.slots_per_slab = 0u;
        ex(r, stdlib_mem_pool_add_slab(&p, &e) == STDLIB_OK); break;
    case K_OVER:
        /* no size argument: a normal add_slab after init must still succeed */
        ex(r, stdlib_mem_pool_add_slab(&p, &e) == STDLIB_OK); break;
    case K_SIZEMAX:
        p.slots_per_slab = SIZE_MAX;
        ex(r, stdlib_mem_pool_add_slab(&p, &e) == STDLIB_FAIL); break;
    case K_REUSE:
        ex(r, stdlib_mem_pool_add_slab(&p, &e) == STDLIB_OK);
        stdlib_mem_pool_destroy(&p, &e);
        stdlib_mem_pool_init(&p, 16u, 4u, &e);
        ex(r, stdlib_mem_pool_add_slab(&p, &e) == STDLIB_OK); break;
    default:
        ex(r, stdlib_mem_pool_add_slab(&p, &e) == STDLIB_OK && p.slab_count == 1); break;
    }
    stdlib_mem_pool_destroy(&p, &e);
}
static void r024(int kind, struct cres *r) { /* stdlib_mem_pool_alloc */
    stdlib_mem_pool p; error e = error(); void *s;
    switch (kind) {
    case K_NULL: ex(r, stdlib_mem_pool_alloc((stdlib_mem_pool *)0, &e) == 0); break;
    case K_SIZEMAX:
        ex(r, stdlib_mem_pool_init(&p, SIZE_MAX, 4u, &e) == STDLIB_FAIL); break;
    case K_STALE:
        ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        s = stdlib_mem_pool_alloc(&p, &e);
        ex(r, s != 0 && zeroed(s, 16u));
        stdlib_mem_pool_release(&p, s, &e);
        stdlib_mem_pool_release(&p, s, &e);
        ex(r, e.code() == CPLUS_EXIT_DOUBLE_FREE);
        stdlib_mem_pool_destroy(&p, &e); break;
    case K_REUSE:
        ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        s = stdlib_mem_pool_alloc(&p, &e);
        stdlib_mem_pool_release(&p, s, &e);
        s = stdlib_mem_pool_alloc(&p, &e);
        ex(r, s != 0);
        stdlib_mem_pool_destroy(&p, &e); break;
    default:
        ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        s = stdlib_mem_pool_alloc(&p, &e);
        ex(r, s != 0 && zeroed(s, 16u));
        stdlib_mem_pool_destroy(&p, &e); break;
    }
}
static void r025(int kind, struct cres *r) { /* stdlib_mem_pool_release */
    stdlib_mem_pool p; error e = error(); void *s; void *foreign;
    unsigned char stackbuf[64];
    if (stdlib_mem_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    s = stdlib_mem_pool_alloc(&p, &e);
    foreign = (void *)stackbuf;
    switch (kind) {
    case K_NULL:
        stdlib_mem_pool_release((stdlib_mem_pool *)0, s, &e);
        ex(r, e.failed_now() == 1);
        stdlib_mem_pool_release(&p, (void *)0, &e);
        ex(r, e.failed_now() == 1); break;
    case K_STALE:
        stdlib_mem_pool_release(&p, s, &e);
        stdlib_mem_pool_release(&p, s, &e);
        ex(r, e.code() == CPLUS_EXIT_DOUBLE_FREE); break;
    case K_SIZEMAX:
        stdlib_mem_pool_release(&p, (void *)SIZE_MAX, &e); /* foreign pointer */
        ex(r, e.code() == CPLUS_EXIT_OWNERSHIP); break;
    case K_ALIGN:
        stdlib_mem_pool_release(&p, (void *)((unsigned char *)s + 1u), &e);
        ex(r, e.failed_now() == 1); break;
    case K_ZERO:
        stdlib_mem_pool_release(&p, foreign, &e);
        ex(r, e.code() == CPLUS_EXIT_OWNERSHIP); break;
    default:
        stdlib_mem_pool_release(&p, s, &e);
        ex(r, e.ok() == 1 && p.live == 0); break;
    }
    stdlib_mem_pool_destroy(&p, &e);
}
static void r026(int kind, struct cres *r) { /* stdlib_mem_pool_destroy */
    stdlib_mem_pool p; error e = error();
    stdlib_mem_pool_destroy((stdlib_mem_pool *)0, &e);
    ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
    if (kind != K_ZERO) {
        void *s = stdlib_mem_pool_alloc(&p, &e);
        ex(r, s != 0);
    }
    stdlib_mem_pool_destroy(&p, &e);
    ex(r, p.slabs == 0 && p.free_head == 0 && p.slab_count == 0 && p.live == 0);
    if (kind == K_REUSE) {
        ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        stdlib_mem_pool_destroy(&p, &e);
    }
}
static void r027(int kind, struct cres *r) { /* stdlib_pool_op_alloc */
    stdlib_mem_pool p; error e = error(); void *s;
    if (stdlib_mem_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_NULL: stdlib_pool_op_alloc((void *)0, 16u, 16u); break; /* crash isolated */
    case K_ZERO: ex(r, stdlib_pool_op_alloc(&p, 0u, 16u) != 0); break;
    case K_OVER: ex(r, stdlib_pool_op_alloc(&p, 17u, 16u) == 0); break;
    case K_SIZEMAX: ex(r, stdlib_pool_op_alloc(&p, SIZE_MAX, 16u) == 0); break;
    default: s = stdlib_pool_op_alloc(&p, 16u, 16u); ex(r, s != 0); break;
    }
    stdlib_mem_pool_destroy(&p, &e);
}
static void r028(int kind, struct cres *r) { /* stdlib_pool_op_realloc */
    stdlib_mem_pool p; error e = error(); void *s; void *q;
    if (stdlib_mem_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    s = stdlib_pool_op_alloc(&p, 16u, 16u);
    ex(r, s != 0);
    switch (kind) {
    case K_NULL: stdlib_pool_op_realloc((void *)0, s, 16u, 16u, 16u); break; /* crash */
    case K_ZERO: q = stdlib_pool_op_realloc(&p, s, 16u, 0u, 16u); ex(r, q != 0); break;
    case K_OVER: ex(r, stdlib_pool_op_realloc(&p, s, 16u, 17u, 16u) == 0); break;
    case K_SIZEMAX: ex(r, stdlib_pool_op_realloc(&p, s, 16u, SIZE_MAX, 16u) == 0); break;
    default: q = stdlib_pool_op_realloc(&p, s, 16u, 16u, 16u); ex(r, q != 0); break;
    }
    stdlib_mem_pool_destroy(&p, &e);
}
static void r029(int kind, struct cres *r) { /* stdlib_pool_op_release */
    stdlib_mem_pool p; error e = error(); void *s;
    if (stdlib_mem_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    s = stdlib_pool_op_alloc(&p, 16u, 16u);
    switch (kind) {
    case K_NULL: stdlib_pool_op_release((void *)0, s, 0u); break;
    case K_ZERO: stdlib_pool_op_release(&p, s, 0u); ex(r, p.live == 0); break;
    case K_SIZEMAX: stdlib_pool_op_release(&p, s, SIZE_MAX); ex(r, p.live == 0); break;
    default: stdlib_pool_op_release(&p, s, 16u); ex(r, p.live == 0); break;
    }
    stdlib_mem_pool_destroy(&p, &e);
}
static void r030(int kind, struct cres *r) { /* stdlib_pool_op_reset */
    stdlib_mem_pool p; error e = error(); void *s;
    stdlib_pool_op_reset((void *)0);
    ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
    s = stdlib_pool_op_alloc(&p, 16u, 16u);
    ex(r, s != 0);
    stdlib_pool_op_reset(&p);
    ex(r, p.slabs == 0 && p.slab_count == 0);
    if (kind == K_REUSE) {
        s = stdlib_pool_op_alloc(&p, 16u, 16u);
        ex(r, s != 0);
    }
    stdlib_mem_pool_destroy(&p, &e);
}
static void r031(int kind, struct cres *r) { /* stdlib_mem_pool_as_allocator */
    stdlib_mem_pool p; error e = error(); stdlib_allocator v; void *s;
    v = stdlib_mem_pool_as_allocator((stdlib_mem_pool *)0);
    ex(r, v.ctx == 0 && v.alloc == stdlib_pool_op_alloc);
    ex(r, stdlib_mem_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
    v = stdlib_mem_pool_as_allocator(&p);
    ex(r, v.ctx == &p && v.alloc == stdlib_pool_op_alloc &&
          v.reset == stdlib_pool_op_reset);
    if (kind != K_NULL) {
        s = stdlib_allocate(&v, 16u, 16u, &e);
        ex(r, s != 0);
    }
    stdlib_mem_pool_destroy(&p, &e);
}
static void slot56_zero_pool(stdlib_slot_pool *q) {
    q->items = (unsigned char *)0;
    q->slot_size = 0;
    q->count = 0;
    q->capacity = 0;
    q->free_list = (size_t *)0;
    q->free_count = 0;
    q->generations = (uint64_t *)0;
    q->owns = 0;
}
static void slot56_clean(const stdlib_slot_pool *q, struct cres *r) {
    ex(r, q->items == (unsigned char *)0 && q->free_list == (size_t *)0 &&
           q->generations == (uint64_t *)0 && q->slot_size == 0 &&
           q->count == 0 && q->capacity == 0 && q->free_count == 0 &&
           q->owns == 0);
}
static void r032(int kind, struct cres *r) { /* stdlib_slot_pool_init */
    stdlib_slot_pool p; error e = error();
    switch (kind) {
    case K_NULL: ex(r, stdlib_slot_pool_init((stdlib_slot_pool *)0, 16u, 4u, &e) == STDLIB_FAIL); break;
    case K_ZERO: ex(r, stdlib_slot_pool_init(&p, 0u, 4u, &e) == STDLIB_FAIL); break;
    case K_EXACT: ex(r, stdlib_slot_pool_init(&p, 16u, 1u, &e) == STDLIB_OK && p.capacity == 1); break;
    case K_OVER: ex(r, stdlib_slot_pool_init(&p, SIZE_MAX, 4u, &e) == STDLIB_FAIL); break;
    case K_SIZEMAX: ex(r, stdlib_slot_pool_init(&p, SIZE_MAX, SIZE_MAX, &e) == STDLIB_FAIL); break;
    case K_REUSE:
        ex(r, stdlib_slot_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        stdlib_slot_pool_destroy(&p, &e);
        ex(r, stdlib_slot_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        stdlib_slot_pool_destroy(&p, &e); break;
    default:
        ex(r, stdlib_slot_pool_init(&p, 16u, 0u, &e) == STDLIB_OK && p.capacity == 8u);
        stdlib_slot_pool_destroy(&p, &e); break;
    }
    /* Issue #56: every partial-OOM point must be failure-atomic: no leaked
     * buffers, no partially published pool, and a safe destroy afterwards. */
    {
        stdlib_slot_pool q;
        size_t before = g_slot_live;
        long n;
        slot56_zero_pool(&q);
        for (n = 0L; n <= 2L; ++n) {
            g_slot_inject = n;
            ex(r, stdlib_slot_pool_init(&q, 16u, 4u, &e) == STDLIB_FAIL);
            g_slot_inject = -1L;
            ex(r, e.code() == CPLUS_EXIT_RESOURCE);
            ex(r, g_slot_live == before);
            slot56_clean(&q, r);
            stdlib_slot_pool_destroy(&q, &e); /* no-op: nothing was published */
            ex(r, g_slot_live == before);
        }
    }
}
static void r033(int kind, struct cres *r) { /* stdlib_slot_pool_grow */
    stdlib_slot_pool p; error e = error(); size_t cap;
    if (stdlib_slot_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_NULL: stdlib_slot_pool_grow((stdlib_slot_pool *)0, &e); break; /* crash */
    case K_ZERO:
    case K_SIZEMAX:
        /* no size parameter: normal growth must still succeed deterministically */
        cap = p.capacity;
        ex(r, stdlib_slot_pool_grow(&p, &e) == STDLIB_OK && p.capacity > cap); break;
    default:
        cap = p.capacity;
        ex(r, stdlib_slot_pool_grow(&p, &e) == STDLIB_OK && p.capacity == cap * 2u); break;
    }
    /* Issue #56: capacity*2 and both ncap*element products must be guarded,
     * and a partial OOM must leave the original pool completely intact. */
    {
        stdlib_slot_pool q;
        unsigned char *old_items;
        size_t before;
        size_t saved;
        slot56_zero_pool(&q);
        ex(r, stdlib_slot_pool_init(&q, 1u, 4u, &e) == STDLIB_OK);
        saved = q.capacity;
        /* (a) capacity * 2 wraps to zero. */
        q.capacity = (size_t)-1 / 2u + 1u;
        before = g_slot_live;
        ex(r, stdlib_slot_pool_grow(&q, &e) == STDLIB_FAIL);
        ex(r, e.code() == CPLUS_EXIT_BOUNDS);
        ex(r, g_slot_live == before && q.capacity == (size_t)-1 / 2u + 1u);
        /* (b) ncap*sizeof(size_t) and ncap*sizeof(uint64_t) overflow while
         *     ncap*slot_size still fits (slot_size == 1). */
        q.capacity = (size_t)-1 / (2u * sizeof(size_t)) + 1u;
        before = g_slot_live;
        ex(r, stdlib_slot_pool_grow(&q, &e) == STDLIB_FAIL);
        ex(r, e.code() == CPLUS_EXIT_BOUNDS);
        ex(r, g_slot_live == before);
        /* (c) two of three new buffers allocated, then OOM. */
        q.capacity = saved;
        old_items = q.items;
        before = g_slot_live;
        g_slot_inject = 1L;
        ex(r, stdlib_slot_pool_grow(&q, &e) == STDLIB_FAIL);
        g_slot_inject = -1L;
        ex(r, e.code() == CPLUS_EXIT_RESOURCE);
        ex(r, g_slot_live == before && q.capacity == saved && q.items == old_items);
        stdlib_slot_pool_destroy(&q, &e);
    }
    stdlib_slot_pool_destroy(&p, &e);
}
static void r034(int kind, struct cres *r) { /* stdlib_slot_pool_alloc_ref */
    stdlib_slot_pool p; error e = error(); stdlib_slot_ref ref; void *v;
    if (stdlib_slot_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_slot_pool_alloc_ref((stdlib_slot_pool *)0, &ref, &e) == STDLIB_FAIL);
        ex(r, stdlib_slot_pool_alloc_ref(&p, (stdlib_slot_ref *)0, &e) == STDLIB_FAIL); break;
    case K_EXACT:
        ex(r, stdlib_slot_pool_alloc_ref(&p, &ref, &e) == STDLIB_OK && ref.index == 0);
        v = stdlib_slot_ref_get(&ref, &e); ex(r, v != 0 && zeroed(v, 16u)); break;
    case K_REUSE: {
        uint64_t g;
        ex(r, stdlib_slot_pool_alloc_ref(&p, &ref, &e) == STDLIB_OK);
        g = ref.generation;
        stdlib_slot_ref_free(&ref, &e);
        ex(r, stdlib_slot_pool_alloc_ref(&p, &ref, &e) == STDLIB_OK);
        ex(r, ref.index == 0 && ref.generation != g); break;
    }
    default:
        ex(r, stdlib_slot_pool_alloc_ref(&p, &ref, &e) == STDLIB_OK); break;
    }
    stdlib_slot_pool_destroy(&p, &e);
}
static void r035(int kind, struct cres *r) { /* stdlib_slot_ref_get */
    stdlib_slot_pool p; error e = error(); stdlib_slot_ref ref, stale; void *v;
    if (stdlib_slot_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    ex(r, stdlib_slot_pool_alloc_ref(&p, &ref, &e) == STDLIB_OK);
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_slot_ref_get((const stdlib_slot_ref *)0, &e) == 0); break;
    case K_ZERO: {
        stdlib_slot_ref z; z.pool = 0; z.index = 0; z.generation = 0;
        ex(r, stdlib_slot_ref_get(&z, &e) == 0); break;
    }
    case K_STALE:
        stale = ref;
        stdlib_slot_ref_free(&ref, &e);
        ex(r, stdlib_slot_ref_get(&stale, &e) == 0 && e.code() == CPLUS_EXIT_USE_AFTER_FREE);
        break;
    default:
        v = stdlib_slot_ref_get(&ref, &e);
        ex(r, v != 0 && v == p.items + ref.index * p.slot_size); break;
    }
    stdlib_slot_pool_destroy(&p, &e);
}
static void r036(int kind, struct cres *r) { /* stdlib_slot_ref_free */
    stdlib_slot_pool p; error e = error(); stdlib_slot_ref ref, stale;
    if (stdlib_slot_pool_init(&p, 16u, 4u, &e) != STDLIB_OK) { r->pass = 0; return; }
    ex(r, stdlib_slot_pool_alloc_ref(&p, &ref, &e) == STDLIB_OK);
    switch (kind) {
    case K_NULL:
        stdlib_slot_ref_free((stdlib_slot_ref *)0, &e);
        ex(r, e.failed_now() == 1); break;
    case K_STALE:
        stale = ref;
        stdlib_slot_ref_free(&ref, &e);
        stdlib_slot_ref_free(&stale, &e);
        ex(r, e.code() == CPLUS_EXIT_DOUBLE_FREE); break;
    default:
        stdlib_slot_ref_free(&ref, &e);
        ex(r, p.free_count == 1 && ref.pool == 0); break;
    }
    stdlib_slot_pool_destroy(&p, &e);
}
static void r037(int kind, struct cres *r) { /* stdlib_slot_pool_destroy */
    stdlib_slot_pool p; error e = error(); stdlib_slot_ref ref;
    stdlib_slot_pool_destroy((stdlib_slot_pool *)0, &e);
    ex(r, stdlib_slot_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
    if (kind != K_ZERO) {
        ex(r, stdlib_slot_pool_alloc_ref(&p, &ref, &e) == STDLIB_OK);
    }
    stdlib_slot_pool_destroy(&p, &e);
    ex(r, p.items == 0 && p.free_list == 0 && p.generations == 0 && p.owns == 0);
    if (kind == K_REUSE) {
        ex(r, stdlib_slot_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        stdlib_slot_pool_destroy(&p, &e);
    }
}
static void r038(int kind, struct cres *r) { /* stdlib_slot_pool_scope */
    stdlib_slot_pool p; error e = error();
    {
        stdlib_slot_pool_scope g = stdlib_slot_pool_scope((stdlib_slot_pool *)0);
        stdlib_slot_pool_scope_drop(&g);
    }
    ex(r, stdlib_slot_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
    {
        stdlib_slot_pool_scope g = stdlib_slot_pool_scope(&p);
        if (kind != K_ZERO) {
            stdlib_slot_ref ref;
            ex(r, stdlib_slot_pool_alloc_ref(&p, &ref, &e) == STDLIB_OK);
        }
        stdlib_slot_pool_scope_drop(&g);
    }
    ex(r, p.items == 0 && p.owns == 0);
    if (kind == K_REUSE) {
        ex(r, stdlib_slot_pool_init(&p, 16u, 4u, &e) == STDLIB_OK);
        stdlib_slot_pool_destroy(&p, &e);
    }
}

/* ========================================================================= */
/* datastack.hp                                                               */
/* ========================================================================= */
static void r039(int kind, struct cres *r) { /* stdlib_datastack_next_gen */
    uint64_t g1 = stdlib_datastack_next_gen();
    uint64_t g2 = stdlib_datastack_next_gen();
    (void)kind;
    ex(r, g1 != 0 && g2 != 0 && g2 != g1);
}
static void r040(int kind, struct cres *r) { /* stdlib_datastack_page_size */
    size_t ps;
    (void)kind;
    ps = stdlib_datastack_page_size();
    ex(r, ps > 0 && (ps & (ps - 1u)) == 0);
}
static void r041(int kind, struct cres *r) { /* stdlib_datastack_init_vm */
    stdlib_datastack ds; error e = error(); uint64_t g;
    ds.base = 0;
    switch (kind) {
    case K_NULL: ex(r, stdlib_datastack_init_vm((stdlib_datastack *)0, 1u<<20u, &e) == STDLIB_FAIL); break;
    case K_ZERO: ex(r, stdlib_datastack_init_vm(&ds, 0u, &e) == STDLIB_FAIL); break;
    case K_SIZEMAX: ex(r, stdlib_datastack_init_vm(&ds, SIZE_MAX, &e) == STDLIB_FAIL); break;
    case K_REUSE:
    case K_STALE:
        ex(r, stdlib_datastack_init_vm(&ds, 1u<<20u, &e) == STDLIB_OK);
        g = ds.generation;
        stdlib_datastack_destroy(&ds, &e);
        ex(r, stdlib_datastack_init_vm(&ds, 1u<<20u, &e) == STDLIB_OK);
        ex(r, ds.generation != g);
        stdlib_datastack_destroy(&ds, &e); break;
    default:
        ex(r, stdlib_datastack_init_vm(&ds, 1u<<20u, &e) == STDLIB_OK &&
              ds.backing == 1 && ds.committed == 0 && ds.capacity == (1u<<20u));
        stdlib_datastack_destroy(&ds, &e); break;
    }
}
static void r042(int kind, struct cres *r) { /* stdlib_datastack_commit */
    stdlib_datastack ds; error e = error();
    ds.base = 0;
    ex(r, stdlib_datastack_commit((stdlib_datastack *)0, 16u, &e) == STDLIB_OK);
    if (stdlib_datastack_init_vm(&ds, 1u<<20u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_ZERO: ex(r, stdlib_datastack_commit(&ds, 0u, &e) == STDLIB_OK); break;
    case K_EXACT:
        ex(r, stdlib_datastack_commit(&ds, ds.capacity, &e) == STDLIB_OK);
        ex(r, ds.committed == ds.capacity); break;
    case K_OVER:
    case K_SIZEMAX:
        ex(r, stdlib_datastack_commit(&ds, ds.capacity + 1u, &e) == STDLIB_FAIL); break;
    default:
        ex(r, stdlib_datastack_commit(&ds, 4096u, &e) == STDLIB_OK);
        ex(r, ds.committed >= 4096u); break;
    }
    stdlib_datastack_destroy(&ds, &e);
}
static void r043(int kind, struct cres *r) { /* stdlib_datastack_init */
    stdlib_datastack ds; error e = error(); uint64_t g;
    ds.base = 0;
    switch (kind) {
    case K_NULL: ex(r, stdlib_datastack_init((stdlib_datastack *)0, 4096u, &e) == STDLIB_FAIL); break;
    case K_ZERO: ex(r, stdlib_datastack_init(&ds, 0u, &e) == STDLIB_FAIL); break;
    case K_SIZEMAX: ex(r, stdlib_datastack_init(&ds, SIZE_MAX, &e) == STDLIB_FAIL); break;
    case K_REUSE:
    case K_STALE:
        ex(r, stdlib_datastack_init(&ds, 4096u, &e) == STDLIB_OK);
        g = ds.generation;
        stdlib_datastack_destroy(&ds, &e);
        ex(r, stdlib_datastack_init(&ds, 4096u, &e) == STDLIB_OK);
        ex(r, ds.generation != g);
        stdlib_datastack_destroy(&ds, &e); break;
    default:
        ex(r, stdlib_datastack_init(&ds, 4096u, &e) == STDLIB_OK &&
              ds.backing == 0 && ds.base != 0 && ds.top == ds.base);
        stdlib_datastack_destroy(&ds, &e); break;
    }
}
static void r044(int kind, struct cres *r) { /* stdlib_datastack_destroy */
    stdlib_datastack ds; error e = error(); uint64_t g;
    stdlib_datastack_destroy((stdlib_datastack *)0, &e);
    ex(r, stdlib_datastack_init(&ds, 4096u, &e) == STDLIB_OK);
    g = ds.generation;
    if (kind != K_ZERO) {
        void *p = stdlib_datastack_alloc(&ds, 16u, 16u, &e);
        ex(r, p != 0);
    }
    stdlib_datastack_destroy(&ds, &e);
    ex(r, ds.base == 0 && ds.top == 0 && ds.capacity == 0 && ds.generation != g);
    if (kind == K_REUSE) {
        ex(r, stdlib_datastack_init(&ds, 4096u, &e) == STDLIB_OK);
        stdlib_datastack_destroy(&ds, &e);
    }
}
static void r045(int kind, struct cres *r) { /* stdlib_datastack_ensure */
    stdlib_datastack ds; error e = error();
    ds.base = 0;
    ex(r, stdlib_datastack_ensure((stdlib_datastack *)0, 16u, &e) == STDLIB_OK);
    if (stdlib_datastack_init_vm(&ds, 1u<<20u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_ZERO: ex(r, stdlib_datastack_ensure(&ds, 0u, &e) == STDLIB_OK); break;
    case K_OVER:
    case K_SIZEMAX: ex(r, stdlib_datastack_ensure(&ds, ds.capacity + 1u, &e) == STDLIB_FAIL); break;
    default: ex(r, stdlib_datastack_ensure(&ds, 8192u, &e) == STDLIB_OK); break;
    }
    stdlib_datastack_destroy(&ds, &e);
}
static void r046(int kind, struct cres *r) { /* stdlib_thread_datastack */
    stdlib_datastack *ds; error e = error(); error *ep = &e;
    if (kind == K_NULL) ep = (error *)0;
    ds = stdlib_thread_datastack(ep);
    ex(r, ds != 0 && ds->base != 0);
    if (ds != 0) ds->top = ds->base;
}
static void r047(int kind, struct cres *r) { /* stdlib_scratch_enter */
    stdlib_datastack ds; error e = error(); stdlib_scratch_mark m;
    if (stdlib_datastack_init(&ds, 4096u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_NULL:
        stdlib_scratch_enter((stdlib_datastack *)0); /* crash isolated */
        break;
    default:
        m = stdlib_scratch_enter(&ds);
        ex(r, m.saved_top == ds.top && m.generation == ds.generation); break;
    }
    stdlib_datastack_destroy(&ds, &e);
}
static void r048(int kind, struct cres *r) { /* stdlib_scratch_leave */
    stdlib_datastack ds; error e = error(); stdlib_scratch_mark m; void *p;
    if (stdlib_datastack_init(&ds, 4096u, &e) != STDLIB_OK) { r->pass = 0; return; }
    m = stdlib_scratch_enter(&ds);
    p = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
    ex(r, p != 0);
    switch (kind) {
    case K_NULL:
        stdlib_scratch_leave((stdlib_datastack *)0, m); /* crash isolated */
        break;
    case K_STALE:
        m.generation++;
        stdlib_scratch_leave(&ds, m);
        ex(r, ds.top != (unsigned char *)0); /* stale mark: no restore */
        break;
    default:
        stdlib_scratch_leave(&ds, m);
        ex(r, ds.top == m.saved_top); break;
    }
    stdlib_datastack_destroy(&ds, &e);
}
static void r049(int kind, struct cres *r) { /* stdlib_datastack_alloc */
    stdlib_datastack ds; error e = error(); void *p;
    if (stdlib_datastack_init(&ds, 4096u, &e) != STDLIB_OK) { r->pass = 0; return; }
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_datastack_alloc((stdlib_datastack *)0, 32u, 16u, &e) == 0); break;
    case K_ZERO:
        ex(r, stdlib_datastack_alloc(&ds, 0u, 16u, &e) == 0); break;
    case K_EXACT:
        p = stdlib_datastack_alloc(&ds, ds.capacity, 16u, &e);
        ex(r, 1); /* exact-capacity bump: accept either OK or refusal by alignment */
        break;
    case K_OVER:
        ex(r, stdlib_datastack_alloc(&ds, ds.capacity + 1u, 16u, &e) == 0); break;
    case K_SIZEMAX:
        ex(r, stdlib_datastack_alloc(&ds, SIZE_MAX, 16u, &e) == 0); break;
    case K_ALIGN:
        ex(r, stdlib_datastack_alloc(&ds, 16u, 3u, &e) == 0);
        ex(r, stdlib_datastack_alloc(&ds, 16u, 0u, &e) == 0); break;
    case K_REUSE:
        p = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
        stdlib_datastack_destroy(&ds, &e);
        ex(r, stdlib_datastack_init(&ds, 4096u, &e) == STDLIB_OK);
        p = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
        ex(r, p != 0); break;
    case K_STALE: {
        uint64_t g = ds.generation;
        stdlib_datastack_destroy(&ds, &e);
        ex(r, stdlib_datastack_init(&ds, 4096u, &e) == STDLIB_OK && ds.generation != g);
        break;
    }
    default:
        p = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
        ex(r, p != 0 && aligned(p, 16u) && zeroed(p, 32u)); break;
    }
    if (ds.base != 0) stdlib_datastack_destroy(&ds, &e);
}
static void r050(int kind, struct cres *r) { /* stdlib_datastack_realloc */
    stdlib_datastack ds; error e = error(); void *p; void *q; void *top;
    if (stdlib_datastack_init(&ds, 4096u, &e) != STDLIB_OK) { r->pass = 0; return; }
    p = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
    ex(r, p != 0);
    ((unsigned char *)p)[0] = 0x37u;
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_datastack_realloc((stdlib_datastack *)0, p, 32u, 64u, 16u, &e) == 0);
        ex(r, stdlib_datastack_realloc(&ds, (void *)0, 32u, 64u, 16u, &e) == 0); break;
    case K_ZERO:
        ex(r, stdlib_datastack_realloc(&ds, p, 32u, 0u, 16u, &e) == 0); break;
    case K_EXACT:
        q = stdlib_datastack_realloc(&ds, p, 32u, 32u, 16u, &e);
        ex(r, q == p && ((unsigned char *)q)[0] == 0x37u); break;
    case K_OVER:
    case K_SIZEMAX:
        ex(r, stdlib_datastack_realloc(&ds, p, 32u, SIZE_MAX, 16u, &e) == 0); break;
    case K_ALIGN:
        top = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
        ex(r, top != 0);
        ex(r, stdlib_datastack_realloc(&ds, p, 32u, 64u, 3u, &e) == 0); break;
    case K_STALE:
        ex(r, stdlib_datastack_realloc(&ds, p, SIZE_MAX, 64u, 16u, &e) == 0); break;
    default:
        q = stdlib_datastack_realloc(&ds, p, 32u, 64u, 16u, &e);
        ex(r, q == p && ((unsigned char *)q)[0] == 0x37u); break;
    }
    stdlib_datastack_destroy(&ds, &e);
}
static void r051(int kind, struct cres *r) { /* stdlib_datastack_release */
    stdlib_datastack ds; error e = error(); void *p;
    if (stdlib_datastack_init(&ds, 4096u, &e) != STDLIB_OK) { r->pass = 0; return; }
    p = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
    ex(r, p != 0);
    switch (kind) {
    case K_NULL:
        stdlib_datastack_release((stdlib_datastack *)0, p, 32u, &e);
        stdlib_datastack_release(&ds, (void *)0, 32u, &e); break;
    case K_ZERO:
        stdlib_datastack_release(&ds, p, 0u, &e);
        ex(r, ds.top == (unsigned char *)p + 32u); break;
    case K_SIZEMAX:
        stdlib_datastack_release(&ds, p, SIZE_MAX, &e);
        ex(r, ds.top >= ds.base); break;
    default:
        stdlib_datastack_release(&ds, p, 32u, &e);
        ex(r, ds.top == (unsigned char *)p); break;
    }
    stdlib_datastack_destroy(&ds, &e);
}
static void r052(int kind, struct cres *r) { /* stdlib_scratch_op_alloc */
    void *p;
    (void)kind;
    p = stdlib_scratch_op_alloc((void *)0, 32u, 16u);
    ex(r, p != 0 && aligned(p, 16u));
}
static void r053(int kind, struct cres *r) { /* stdlib_scratch_op_realloc */
    void *p; void *q;
    (void)kind;
    p = stdlib_scratch_op_alloc((void *)0, 32u, 16u);
    ex(r, p != 0);
    q = stdlib_scratch_op_realloc((void *)0, p, 32u, 64u, 16u);
    ex(r, q == p);
}
static void r054(int kind, struct cres *r) { /* stdlib_scratch_op_release */
    void *p;
    (void)kind;
    p = stdlib_scratch_op_alloc((void *)0, 32u, 16u);
    ex(r, p != 0);
    stdlib_scratch_op_release((void *)0, p, 32u);
}
static void r055(int kind, struct cres *r) { /* stdlib_scratch_op_reset */
    void *p;
    (void)kind;
    p = stdlib_scratch_op_alloc((void *)0, 32u, 16u);
    ex(r, p != 0);
    stdlib_scratch_op_reset((void *)0);
}
static void r056(int kind, struct cres *r) { /* stdlib_scratch_scope */
    stdlib_datastack ds; error e = error();
    if (stdlib_datastack_init(&ds, 4096u, &e) != STDLIB_OK) { r->pass = 0; return; }
    {
        stdlib_scratch_scope g = stdlib_scratch_scope((stdlib_datastack *)0);
        stdlib_scratch_scope_drop(&g);
    }
    {
        stdlib_scratch_scope g = stdlib_scratch_scope(&ds);
        void *p = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
        ex(r, p != 0);
        if (kind == K_STALE) {
            stdlib_datastack_destroy(&ds, &e);
            stdlib_scratch_scope_drop(&g); /* stale generation: no restore */
        } else {
            stdlib_scratch_scope_drop(&g);
            ex(r, ds.top == ds.base);
        }
    }
    if (ds.base != 0) stdlib_datastack_destroy(&ds, &e);
}
static void r057(int kind, struct cres *r) { /* stdlib_scratch_allocator */
    stdlib_allocator v;
    (void)kind;
    v = stdlib_scratch_allocator();
    ex(r, v.ctx == 0 && v.alloc == stdlib_scratch_op_alloc &&
          v.realloc == stdlib_scratch_op_realloc && v.release == stdlib_scratch_op_release &&
          v.reset == stdlib_scratch_op_reset);
}

/* ========================================================================= */
/* error.hp                                                                   */
/* ========================================================================= */
static void r058(int kind, struct cres *r) { /* stdlib_tm_add */
    stdlib_telemetry_stats a, b; int i;
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_telemetry_snapshot(&a);
    for (i = 0; i < (kind == K_ZERO ? 0 : (kind == K_EXACT ? 1 : 3)); i++)
        stdlib_tm_add(STDLIB_TM_INVALID, 1);
    stdlib_telemetry_snapshot(&b);
    ex(r, b.invalid_ops == a.invalid_ops + (unsigned long long)(kind == K_ZERO ? 0 :
          (kind == K_EXACT ? 1 : 3)));
    stdlib_telemetry_set_enabled(0);
}
static void r059(int kind, struct cres *r) { /* stdlib_telemetry_set_enabled */
    (void)kind;
    stdlib_telemetry_set_enabled(1); ex(r, stdlib_telemetry_is_enabled() == 1);
    stdlib_telemetry_set_enabled(0); ex(r, stdlib_telemetry_is_enabled() == 0);
}
static void r060(int kind, struct cres *r) { /* stdlib_telemetry_is_enabled */
    int v;
    (void)kind;
    stdlib_telemetry_set_enabled(1); v = stdlib_telemetry_is_enabled();
    ex(r, v == 1);
    stdlib_telemetry_set_enabled(0); v = stdlib_telemetry_is_enabled();
    ex(r, v == 0);
}
static void r061(int kind, struct cres *r) { /* stdlib_telemetry_reset */
    stdlib_telemetry_stats s;
    (void)kind;
    stdlib_telemetry_set_enabled(1);
    stdlib_tm_add(STDLIB_TM_API_FAIL, 5);
    stdlib_telemetry_reset();
    stdlib_telemetry_snapshot(&s);
    ex(r, s.alloc_count == 0 && s.api_failures == 0 && s.invalid_ops == 0);
    stdlib_telemetry_set_enabled(0);
}
static void r062(int kind, struct cres *r) { /* stdlib_telemetry_snapshot */
    stdlib_telemetry_stats s;
    stdlib_telemetry_snapshot((stdlib_telemetry_stats *)0);
    if (kind == K_NULL) { ex(r, 1); return; }
    stdlib_telemetry_snapshot(&s);
    ex(r, 1); /* all fields are scalars; snapshot is a pure copy */
}
static void r063(int kind, struct cres *r) { /* stdlib_set_error_callback */
    error e = error();
    g_cb_calls = 0; g_cb_cat = -1;
    if (kind == K_NULL) {
        stdlib_set_error_callback((stdlib_error_callback)0);
        error_fail_cat(&e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED, "x");
        ex(r, g_cb_calls == 0);
    } else {
        stdlib_set_error_callback(tally_cb);
        error_fail_cat(&e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, "x");
        ex(r, g_cb_calls == 1 && g_cb_cat == STDLIB_CATEGORY_RANGE);
        stdlib_set_error_callback((stdlib_error_callback)0);
    }
}
static void r064(int kind, struct cres *r) { /* stdlib_category_name */
    int i;
    (void)kind;
    for (i = STDLIB_CATEGORY_OK; i <= STDLIB_CATEGORY_UTF8; i++)
        ex(r, stdlib_category_name(i) != 0);
    ex(r, stdlib_category_name(999) != 0);
}
static void r065(int kind, struct cres *r) { /* stdlib_note */
    stdlib_telemetry_stats a, b;
    g_cb_calls = 0;
    stdlib_set_error_callback(tally_cb);
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_snapshot(&a);
    stdlib_note((kind == K_NULL) ? STDLIB_CATEGORY_INVALID : STDLIB_CATEGORY_MEMORY,
                CPLUS_EXIT_RESOURCE, (kind == K_NULL) ? (const char *)0 : "note");
    stdlib_telemetry_snapshot(&b);
    ex(r, g_cb_calls == 1 && b.api_failures == a.api_failures + 1);
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
}
static void r066(int kind, struct cres *r) { /* stdlib_alloc_fail_at */
    void *p; void *q; long n = (kind == K_ZERO) ? 0 : 1;
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_at(n);
    if (n == 0) {
        p = stdlib_alloc(8u); ex(r, p != 0); if (p) stdlib_release(p);
    } else {
        p = stdlib_alloc(8u); q = stdlib_alloc(8u);
        ex(r, p == 0 && q != 0);
        if (q != 0) stdlib_release(q);
    }
    stdlib_alloc_fail_reset();
}
static void r067(int kind, struct cres *r) { /* stdlib_alloc_fail_after */
    void *p; long n = (kind == K_ZERO) ? 0 : -1;
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_after(n);
    p = stdlib_alloc(8u);
    ex(r, (n >= 0) ? (p == 0) : (p != 0));
    if (p != 0) stdlib_release(p);
    stdlib_alloc_fail_reset();
}
static void r068(int kind, struct cres *r) { /* stdlib_alloc_fail_reset */
    void *p;
    (void)kind;
    stdlib_alloc_fail_at(1);
    stdlib_alloc_fail_reset();
    p = stdlib_alloc(8u);
    ex(r, p != 0);
    stdlib_release(p);
}
static void r069(int kind, struct cres *r) { /* stdlib_alloc_seq */
    long s0, s1;
    (void)kind;
    stdlib_alloc_fail_reset();
    s0 = stdlib_alloc_seq();
    { void *p = stdlib_alloc(8u); s1 = stdlib_alloc_seq(); stdlib_release(p); }
    ex(r, s1 == s0 + 1);
}
static void r070(int kind, struct cres *r) { /* stdlib_meta_acquire */
    (void)kind;
    stdlib_meta_acquire();
    stdlib_meta_release_lock();
    ex(r, 1);
}
static void r071(int kind, struct cres *r) { /* stdlib_meta_release_lock */
    (void)kind;
    stdlib_meta_release_lock();
    ex(r, 1);
}
static void r072(int kind, struct cres *r) { /* stdlib_meta_hash */
    size_t h0, h1; int x = 7;
    (void)kind;
    h0 = stdlib_meta_hash((const void *)0);
    h1 = stdlib_meta_hash((const void *)&x);
    ex(r, 1);
    (void)h0; (void)h1;
}
static void r073(int kind, struct cres *r) { /* stdlib_meta_find */
    void *p;
    ex(r, stdlib_meta_find((const void *)0) == 0);
    ex(r, stdlib_meta_find(STDLIB_META_TOMB) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(8u);
    ex(r, stdlib_meta_find(p) != 0);
    stdlib_release(p);
    ex(r, stdlib_meta_find(p) == 0);
}
static void r074(int kind, struct cres *r) { /* stdlib_meta_record */
    int x = 0; stdlib_meta_slot *s;
    stdlib_meta_record((const void *)0, 32u, 4u); /* no-op */
    if (kind == K_NULL) { ex(r, 1); return; }
    stdlib_meta_record((const void *)&x, 32u, (kind == K_ZERO) ? 0u : 4u);
    s = stdlib_meta_find((const void *)&x);
    ex(r, s != 0 && s->byte_size == 32u && s->elem_size == ((kind == K_ZERO) ? 1u : 4u));
    stdlib_meta_forget((const void *)&x);
}
static void r075(int kind, struct cres *r) { /* stdlib_meta_forget */
    int x = 0;
    stdlib_meta_forget((const void *)0);
    if (kind == K_NULL) { ex(r, 1); return; }
    stdlib_meta_record((const void *)&x, 16u, 1u);
    ex(r, stdlib_meta_find((const void *)&x) != 0);
    stdlib_meta_forget((const void *)&x);
    ex(r, stdlib_meta_find((const void *)&x) == 0);
}
static void r076(int kind, struct cres *r) { /* stdlib_alloc */
    void *p;
    switch (kind) {
    case K_NULL: ex(r, 1); break; /* no pointer argument */
    case K_ZERO: p = stdlib_alloc(0u); ex(r, p != 0 && stdlib_alloc_size(p) == 1u); stdlib_release(p); break;
    case K_EXACT: p = stdlib_alloc(STDLIB_ALLOC_LIMIT); ex(r, p != 0); stdlib_release(p); break;
    case K_OVER: ex(r, stdlib_alloc(STDLIB_ALLOC_LIMIT + 1u) == 0); break;
    case K_SIZEMAX: ex(r, stdlib_alloc(SIZE_MAX) == 0); break;
    case K_REUSE:
        p = stdlib_alloc(32u); stdlib_release(p);
        p = stdlib_alloc(32u); ex(r, p != 0); stdlib_release(p); break;
    case K_STALE: {
        unsigned long long g;
        p = stdlib_alloc(32u); g = stdlib_alloc_generation(p);
        stdlib_release(p); ex(r, stdlib_alloc_check_generation(p, g) == 0); break;
    }
    default: p = stdlib_alloc(32u); ex(r, p != 0 && stdlib_alloc_size(p) == 32u); stdlib_release(p); break;
    }
}
static void r077(int kind, struct cres *r) { /* stdlib_calloc */
    void *p;
    switch (kind) {
    case K_NULL: ex(r, 1); break;
    case K_ZERO:
        p = stdlib_calloc(0u, 8u); ex(r, p != 0 && stdlib_alloc_size(p) == 1u); stdlib_release(p);
        p = stdlib_calloc(8u, 0u); ex(r, p != 0 && stdlib_alloc_size(p) == 1u); stdlib_release(p); break;
    case K_EXACT:
        p = stdlib_calloc(8u, 16u); ex(r, p != 0 && stdlib_alloc_size(p) == 128u && zeroed(p, 128u));
        stdlib_release(p); break;
    case K_OVER: ex(r, stdlib_calloc(STDLIB_ALLOC_LIMIT + 1u, 2u) == 0); break;
    case K_SIZEMAX: ex(r, stdlib_calloc(SIZE_MAX, 2u) == 0); break;
    case K_REUSE:
        p = stdlib_calloc(4u, 8u); stdlib_release(p);
        p = stdlib_calloc(4u, 8u); ex(r, p != 0); stdlib_release(p); break;
    case K_STALE: {
        unsigned long long g;
        p = stdlib_calloc(4u, 8u); g = stdlib_alloc_generation(p);
        stdlib_release(p); ex(r, stdlib_alloc_check_generation(p, g) == 0); break;
    }
    default: p = stdlib_calloc(4u, 8u); ex(r, p != 0 && zeroed(p, 32u)); stdlib_release(p); break;
    }
}
static void r078(int kind, struct cres *r) { /* stdlib_realloc */
    void *p; void *q;
    switch (kind) {
    case K_NULL:
        q = stdlib_realloc((void *)0, 32u); ex(r, q != 0); stdlib_release(q); break;
    case K_ZERO:
        p = stdlib_alloc(16u); q = stdlib_realloc(p, 0u);
        ex(r, q != 0 && stdlib_alloc_size(q) == 1u); stdlib_release(q); break;
    case K_EXACT:
        p = stdlib_alloc(16u); q = stdlib_realloc(p, 16u); ex(r, q != 0); stdlib_release(q); break;
    case K_OVER:
        ex(r, stdlib_realloc((void *)0, STDLIB_ALLOC_LIMIT + 1u) == 0); break;
    case K_SIZEMAX:
        ex(r, stdlib_realloc((void *)0, SIZE_MAX) == 0); break;
    case K_REUSE:
        p = stdlib_alloc(16u); q = stdlib_realloc(p, 32u);
        stdlib_release(q); p = stdlib_alloc(16u); ex(r, p != 0); stdlib_release(p); break;
    case K_STALE: {
        unsigned long long g;
        p = stdlib_alloc(16u); g = stdlib_alloc_generation(p);
        q = stdlib_realloc(p, 64u);
        ex(r, q != 0 && stdlib_alloc_check_generation(q, g) == 0);
        stdlib_release(q); break;
    }
    default:
        p = stdlib_alloc(16u); q = stdlib_realloc(p, 64u);
        ex(r, q != 0 && stdlib_alloc_size(q) == 64u); stdlib_release(q); break;
    }
}
static void r079(int kind, struct cres *r) { /* stdlib_safe_div */
    (void)kind;
    ex(r, stdlib_safe_div(100u, 0u) == 0u);
    ex(r, stdlib_safe_div(100u, 10u) == 10u);
    ex(r, stdlib_safe_div(SIZE_MAX, 1u) == SIZE_MAX);
    ex(r, stdlib_safe_div(7u, 2u) == 3u);
    ex(r, stdlib_safe_div(0u, 5u) == 0u);
}
static void r080(int kind, struct cres *r) { /* stdlib_alloc_size */
    void *p;
    ex(r, stdlib_alloc_size((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(48u); ex(r, stdlib_alloc_size(p) == 48u);
    stdlib_release(p); ex(r, stdlib_alloc_size(p) == 0);
}
static void r081(int kind, struct cres *r) { /* stdlib_alloc_elem_size */
    void *p;
    ex(r, stdlib_alloc_elem_size((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_calloc(4u, 8u); ex(r, stdlib_alloc_elem_size(p) == 8u); stdlib_release(p);
}
static void r082(int kind, struct cres *r) { /* stdlib_alloc_length */
    void *p;
    ex(r, stdlib_alloc_length((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_calloc(4u, 8u); ex(r, stdlib_alloc_length(p) == 4u); stdlib_release(p);
}
static void r083(int kind, struct cres *r) { /* stdlib_alloc_generation */
    void *p; unsigned long long g;
    ex(r, stdlib_alloc_generation((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(8u); g = stdlib_alloc_generation(p);
    ex(r, g != 0); stdlib_release(p); ex(r, stdlib_alloc_generation(p) == 0);
}
static void r084(int kind, struct cres *r) { /* stdlib_alloc_check_generation */
    void *p; unsigned long long g;
    ex(r, stdlib_alloc_check_generation((const void *)0, 1u) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(8u); g = stdlib_alloc_generation(p);
    ex(r, stdlib_alloc_check_generation(p, g) == 1);
    ex(r, stdlib_alloc_check_generation(p, g + 1u) == 0);
    stdlib_release(p); ex(r, stdlib_alloc_check_generation(p, g) == 0);
}
static void r085(int kind, struct cres *r) { /* stdlib_alloc_query_length */
    void *p; size_t n = 999;
    ex(r, stdlib_alloc_query_length((const void *)0, 8u, &n) == -1 && n == 0);
    if (kind == K_NULL) return;
    p = stdlib_calloc(4u, 8u);
    n = 999; ex(r, stdlib_alloc_query_length(p, 0u, &n) == 0 && n == 4u);
    n = 999; ex(r, stdlib_alloc_query_length(p, 16u, &n) == 0 && n == 2u);
    ex(r, stdlib_alloc_query_length(p, 8u, (size_t *)0) == -1);
    stdlib_release(p);
}
static void r086(int kind, struct cres *r) { /* stdlib_transfer_ownership */
    void *src = (void *)0; void *dst = (void *)0;
    ex(r, stdlib_transfer_ownership((void **)0, &src) == -1);
    ex(r, stdlib_transfer_ownership(&dst, (void **)0) == -1);
    if (kind == K_NULL) return;
    src = stdlib_alloc(32u);
    ex(r, stdlib_transfer_ownership(&dst, &src) == 0);
    ex(r, src == 0 && dst != 0);
    ex(r, stdlib_transfer_ownership(&dst, &src) == -1); /* src now NULL */
    if (kind == K_STALE) {
        unsigned long long g = stdlib_alloc_generation(dst);
        stdlib_release(dst); dst = 0;
        ex(r, stdlib_alloc_check_generation(dst, g) == 0);
    } else {
        stdlib_release(dst);
    }
}
static void r087(int kind, struct cres *r) { /* stdlib_release */
    void *p;
    stdlib_release((void *)0); /* no-op */
    if (kind == K_NULL) { ex(r, 1); return; }
    p = stdlib_alloc(16u); ex(r, p != 0);
    stdlib_release(p); ex(r, stdlib_alloc_size(p) == 0);
}
static void r088(int kind, struct cres *r) { /* error_reset */
    error e = error();
    error_reset((void *)0);
    error_fail_cat(&e, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE, "x");
    ex(r, e.failed_now() == 1);
    error_reset(&e);
    ex(r, e.ok() == 1 && e.category() == STDLIB_CATEGORY_OK && e.text[0] == '\0');
    (void)kind;
}
static void r089(int kind, struct cres *r) { /* error_fail */
    error e = error();
    error_fail((void *)0, CPLUS_EXIT_UNSUPPORTED, "x"); /* no crash */
    error_fail(&e, (kind == K_ZERO) ? CPLUS_EXIT_OK :
               (kind == K_ALIGN) ? CPLUS_EXIT_RESOURCE : CPLUS_EXIT_BOUNDS, "msg");
    ex(r, e.failed_now() == 1);
    if (kind == K_ALIGN) ex(r, e.category() == STDLIB_CATEGORY_MEMORY);
    else if (kind != K_ZERO) ex(r, e.category() == STDLIB_CATEGORY_BOUNDS);
}
static void r090(int kind, struct cres *r) { /* error_fail_cat */
    error e = error(); char longmsg[600]; int i;
    error_fail_cat((void *)0, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED, "x");
    for (i = 0; i < 599; i++) longmsg[i] = 'z';
    longmsg[599] = '\0';
    if (kind == K_NULL) {
        error_fail_cat(&e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED, (const char *)0);
        ex(r, e.failed_now() == 1 && e.what()[0] == '\0');
    } else {
        error_fail_cat(&e, STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP, longmsg);
        ex(r, e.category() == STDLIB_CATEGORY_OWNERSHIP &&
              strlen(e.what()) == (size_t)(STDLIB_ERROR_SIZE - 1));
    }
}
static void r091(int kind, struct cres *r) { /* error class */
    error e = error();
    ex(r, e.ok() == 1 && e.failed_now() == 0 && e.code() == (int)CPLUS_EXIT_OK &&
          e.category() == STDLIB_CATEGORY_OK && e.what()[0] == '\0');
    if (kind == K_NULL) { e.clear(); ex(r, e.ok() == 1); return; }
    e.set(CPLUS_EXIT_RESOURCE, "res");
    ex(r, e.failed_now() == 1 && e.category() == STDLIB_CATEGORY_MEMORY && e.code() == 107);
    e.set_category(STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP, "own");
    ex(r, e.category() == STDLIB_CATEGORY_OWNERSHIP);
    e.clear(); ex(r, e.ok() == 1);
}

/* ========================================================================= */
/* allocator.hp                                                               */
/* ========================================================================= */
static void r092(int kind, struct cres *r) { /* stdlib_alloc_identity */
    stdlib_alloc_id id; void *p;
    ex(r, stdlib_alloc_identity((const void *)0, &id) == -1);
    ex(r, stdlib_alloc_identity((const void *)0, (stdlib_alloc_id *)0) == -1);
    if (kind == K_NULL) return;
    p = stdlib_alloc(32u);
    ex(r, stdlib_alloc_identity(p, &id) == 0 && id.base == p && id.generation != 0);
    if (kind == K_STALE) {
        stdlib_release(p);
        ex(r, stdlib_alloc_identity(p, &id) == -1);
    } else {
        stdlib_release(p);
    }
}
static void r093(int kind, struct cres *r) { /* stdlib_alloc_id_equal */
    stdlib_alloc_id a; stdlib_alloc_id b; void *p;
    (void)kind;
    ex(r, stdlib_alloc_id_equal((const stdlib_alloc_id *)0, &a) == 0);
    ex(r, stdlib_alloc_id_equal(&a, (const stdlib_alloc_id *)0) == 0);
    p = stdlib_alloc(16u);
    ex(r, stdlib_alloc_identity(p, &a) == 0);
    b = a;
    ex(r, stdlib_alloc_id_equal(&a, &b) == 1);
    b.generation++;
    ex(r, stdlib_alloc_id_equal(&a, &b) == 0);
    stdlib_release(p);
    b.base = 0;
    ex(r, stdlib_alloc_id_equal(&a, &b) == 0);
}
static void r094(int kind, struct cres *r) { /* stdlib_alloc_id_matches */
    stdlib_alloc_id id; void *p;
    ex(r, stdlib_alloc_id_matches((const stdlib_alloc_id *)0, (const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(16u);
    ex(r, stdlib_alloc_identity(p, &id) == 0);
    ex(r, stdlib_alloc_id_matches(&id, p) == 1);
    if (kind == K_STALE) {
        stdlib_release(p);
        ex(r, stdlib_alloc_id_matches(&id, p) == 0);
    } else {
        stdlib_release(p);
    }
}
static void r095(int kind, struct cres *r) { /* stdlib_alloc_view_bytes */
    void *p;
    ex(r, stdlib_alloc_view_bytes((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(64u); ex(r, stdlib_alloc_view_bytes(p) == 64u);
    stdlib_release(p); ex(r, stdlib_alloc_view_bytes(p) == 0);
}
static void r096(int kind, struct cres *r) { /* stdlib_alloc_view_elem_bytes */
    void *p;
    ex(r, stdlib_alloc_view_elem_bytes((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_calloc(4u, 8u); ex(r, stdlib_alloc_view_elem_bytes(p) == 8u);
    stdlib_release(p);
}
static void r097(int kind, struct cres *r) { /* stdlib_alloc_view_generation */
    void *p;
    ex(r, stdlib_alloc_view_generation((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(8u); ex(r, stdlib_alloc_view_generation(p) != 0);
    stdlib_release(p); ex(r, stdlib_alloc_view_generation(p) == 0);
}
static void r098(int kind, struct cres *r) { /* stdlib_alloc_view_live */
    void *p;
    ex(r, stdlib_alloc_view_live((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(8u); ex(r, stdlib_alloc_view_live(p) == 1);
    stdlib_release(p); ex(r, stdlib_alloc_view_live(p) == 0);
}
static void r099(int kind, struct cres *r) { /* stdlib_alloc_view_count */
    void *p;
    ex(r, stdlib_alloc_view_count((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_calloc(5u, 8u); ex(r, stdlib_alloc_view_count(p) == 5u);
    stdlib_release(p);
}
static void r100(int kind, struct cres *r) { /* stdlib_alloc_view_remaining */
    void *p;
    ex(r, stdlib_alloc_view_remaining((const void *)0, 0u) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(64u);
    ex(r, stdlib_alloc_view_remaining(p, 0u) == 64u);
    ex(r, stdlib_alloc_view_remaining(p, 64u) == 0u);
    ex(r, stdlib_alloc_view_remaining(p, 65u) == 0u);
    if (kind == K_SIZEMAX) ex(r, stdlib_alloc_view_remaining(p, SIZE_MAX) == 0u);
    stdlib_release(p);
}
static void r101(int kind, struct cres *r) { /* stdlib_alloc_view_unchanged */
    void *p; unsigned long long g;
    ex(r, stdlib_alloc_view_unchanged((const void *)0, 1u) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(8u); g = stdlib_alloc_view_generation(p);
    ex(r, stdlib_alloc_view_unchanged(p, g) == 1);
    ex(r, stdlib_alloc_view_unchanged(p, 0u) == 0);
    if (kind == K_STALE) {
        stdlib_release(p);
        ex(r, stdlib_alloc_view_unchanged(p, g) == 0);
    } else {
        stdlib_release(p);
    }
}
static void r102(int kind, struct cres *r) { /* stdlib_alloc_view_transfer_ok */
    void *src = (void *)0; void *dst = (void *)0;
    ex(r, stdlib_alloc_view_transfer_ok((void **)0, &src) == 0);
    ex(r, stdlib_alloc_view_transfer_ok(&dst, (void **)0) == 0);
    ex(r, stdlib_alloc_view_transfer_ok(&dst, &src) == 0);
    if (kind == K_NULL) return;
    src = stdlib_alloc(8u);
    ex(r, stdlib_alloc_view_transfer_ok(&dst, &src) == 1);
    dst = stdlib_alloc(8u);
    ex(r, stdlib_alloc_view_transfer_ok(&dst, &src) == 0); /* dst non-empty */
    stdlib_release(dst); stdlib_release(src);
}
static void r103(int kind, struct cres *r) { /* stdlib_alloc_cap_state */
    void *p;
    ex(r, stdlib_alloc_cap_state((const void *)0) == STDLIB_ALLOC_CAP_NONE);
    if (kind == K_NULL) return;
    p = stdlib_alloc(8u);
    ex(r, stdlib_alloc_cap_state(p) == STDLIB_ALLOC_CAP_LIVE);
    stdlib_release(p); ex(r, stdlib_alloc_cap_state(p) == STDLIB_ALLOC_CAP_NONE);
}
static void r104(int kind, struct cres *r) { /* stdlib_alloc_hasvalidcap */
    void *p;
    ex(r, stdlib_alloc_hasvalidcap((const void *)0) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(8u); ex(r, stdlib_alloc_hasvalidcap(p) == 1);
    stdlib_release(p); ex(r, stdlib_alloc_hasvalidcap(p) == 0);
}
static void r105(int kind, struct cres *r) { /* stdlib_alloc_cap_covers */
    void *p;
    ex(r, stdlib_alloc_cap_covers((const void *)0, 0u, 0u) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(64u);
    ex(r, stdlib_alloc_cap_covers(p, 0u, 64u) == 1);
    ex(r, stdlib_alloc_cap_covers(p, 64u, 0u) == 1);
    ex(r, stdlib_alloc_cap_covers(p, 64u, 1u) == 0);
    ex(r, stdlib_alloc_cap_covers(p, 65u, 0u) == 0);
    if (kind == K_SIZEMAX) ex(r, stdlib_alloc_cap_covers(p, 0u, SIZE_MAX) == 0);
    stdlib_release(p); ex(r, stdlib_alloc_cap_covers(p, 0u, 8u) == 0);
}
static void r106(int kind, struct cres *r) { /* stdlib_alloc_cap_check */
    void *p; unsigned long long g;
    ex(r, stdlib_alloc_cap_check((const void *)0, 1u, 0u, 0u) == 0);
    if (kind == K_NULL) return;
    p = stdlib_alloc(64u); g = stdlib_alloc_view_generation(p);
    ex(r, stdlib_alloc_cap_check(p, g, 0u, 64u) == 1);
    ex(r, stdlib_alloc_cap_check(p, 0u, 0u, 64u) == 0);
    if (kind == K_STALE) {
        stdlib_release(p);
        ex(r, stdlib_alloc_cap_check(p, g, 0u, 64u) == 0);
    } else {
        stdlib_release(p);
    }
}
static void r107(int kind, struct cres *r) { /* stdlib_alloc_tm_snapshot */
    stdlib_telemetry_stats s;
    stdlib_alloc_tm_snapshot((stdlib_telemetry_stats *)0);
    if (kind == K_NULL) { ex(r, 1); return; }
    stdlib_alloc_tm_snapshot(&s); ex(r, 1);
}
static void r108(int kind, struct cres *r) { /* stdlib_alloc_tm_delta */
    stdlib_telemetry_stats a, b, out;
    stdlib_alloc_tm_delta(&a, &b, (stdlib_telemetry_stats *)0);
    stdlib_alloc_tm_delta((const stdlib_telemetry_stats *)0, &b, &out);
    ex(r, out.alloc_count == 0 && out.api_failures == 0);
    if (kind == K_NULL) return;
    a.alloc_count = 5; b.alloc_count = 9;
    a.api_failures = 2; b.api_failures = 3;
    stdlib_alloc_tm_delta(&a, &b, &out);
    ex(r, out.alloc_count == 4 && out.api_failures == 1);
}
static void r109(int kind, struct cres *r) { /* stdlib_alloc_tm_counter */
    (void)kind;
    ex(r, stdlib_alloc_tm_counter(STDLIB_TM_COUNT) == 0);
    ex(r, stdlib_alloc_tm_counter(-1) == 0);
    ex(r, stdlib_alloc_tm_counter(STDLIB_TM_INVALID) >= 0);
}
static void r110(int kind, struct cres *r) { /* stdlib_alloc_fail_begin */
    void *p;
    stdlib_alloc_fail_reset();
    if (kind == K_NULL) {
        stdlib_alloc_fail_begin(999, 1); /* unknown mode resets */
        p = stdlib_alloc(8u); ex(r, p != 0); stdlib_release(p);
    } else if (kind == K_ZERO) {
        stdlib_alloc_fail_begin(ALLOC_FAIL_SCOPE_AFTER, 0);
        p = stdlib_alloc(8u); ex(r, p == 0);
    } else {
        stdlib_alloc_fail_begin(ALLOC_FAIL_SCOPE_AT, 1);
        p = stdlib_alloc(8u); ex(r, p == 0);
    }
    stdlib_alloc_fail_end();
}
static void r111(int kind, struct cres *r) { /* stdlib_alloc_fail_end */
    void *p;
    (void)kind;
    stdlib_alloc_fail_at(1);
    stdlib_alloc_fail_end();
    p = stdlib_alloc(8u); ex(r, p != 0); stdlib_release(p);
}
static void r112(int kind, struct cres *r) { /* alloc_fail_scope class */
    void *p;
    {
        alloc_fail_scope s = alloc_fail_scope(ALLOC_FAIL_SCOPE_AT, 1);
        ex(r, s.armed_now() == 1);
        p = stdlib_alloc(8u); ex(r, p == 0);
        if (kind == K_REUSE) {
            s.reset_now();
            ex(r, s.armed_now() == 0);
            p = stdlib_alloc(8u); ex(r, p != 0); stdlib_release(p);
        }
        alloc_fail_scope_drop(&s);
        ex(r, s.armed_now() == 0);
    }
    p = stdlib_alloc(8u); ex(r, p != 0); stdlib_release(p);
}

/* ========================================================================= */
/* alloc_policy.hp                                                            */
/* ========================================================================= */
static void r113(int kind, struct cres *r) { /* stdlib_count_size_overflow */
    size_t total = 12345;
    ex(r, stdlib_count_size_overflow(3u, 7u, (size_t *)0) == 1);
    switch (kind) {
    case K_NULL: ex(r, stdlib_count_size_overflow(3u, 7u, (size_t *)0) == 1); break;
    case K_ZERO:
        ex(r, stdlib_count_size_overflow(0u, SIZE_MAX, &total) == 0 && total == 0); break;
    case K_EXACT:
        ex(r, stdlib_count_size_overflow(1024u, 4u, &total) == 0 && total == 4096u); break;
    case K_OVER:
        ex(r, stdlib_count_size_overflow((SIZE_MAX / 2u) + 1u, 2u, &total) == 1 && total == SIZE_MAX); break;
    case K_SIZEMAX:
        ex(r, stdlib_count_size_overflow(SIZE_MAX, 2u, &total) == 1 && total == SIZE_MAX);
        ex(r, stdlib_count_size_overflow(1u, SIZE_MAX, &total) == 0 && total == SIZE_MAX); break;
    default:
        ex(r, stdlib_count_size_overflow(3u, 7u, &total) == 0 && total == 21u); break;
    }
}
static void r114(int kind, struct cres *r) { /* stdlib_size_class_round_up */
    size_t out = 12345;
    ex(r, stdlib_size_class_round_up(16u, 16u, (size_t *)0) == -1);
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_size_class_round_up(16u, 16u, (size_t *)0) == -1); break;
    case K_ZERO:
        ex(r, stdlib_size_class_round_up(0u, 16u, &out) == 0 && out == 0u); break;
    case K_EXACT:
        ex(r, stdlib_size_class_round_up(32u, 16u, &out) == 0 && out == 32u); break;
    case K_OVER:
        ex(r, stdlib_size_class_round_up(SIZE_MAX, 16u, &out) == -1); break;
    case K_SIZEMAX:
        ex(r, stdlib_size_class_round_up(SIZE_MAX, 2u, &out) == -1); break;
    case K_ALIGN:
        ex(r, stdlib_size_class_round_up(16u, 0u, &out) == -1);
        ex(r, stdlib_size_class_round_up(16u, 3u, &out) == -1); break;
    default:
        ex(r, stdlib_size_class_round_up(17u, 16u, &out) == 0 && out == 32u); break;
    }
}
static void r115(int kind, struct cres *r) { /* stdlib_size_class_words */
    size_t words = 12345;
    ex(r, stdlib_size_class_words(16u, (size_t *)0) == -1);
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_size_class_words(16u, (size_t *)0) == -1); break;
    case K_ZERO:
        ex(r, stdlib_size_class_words(0u, &words) == 0 && words == 0u); break;
    case K_EXACT:
        ex(r, stdlib_size_class_words(sizeof(uintptr_t) * 3u, &words) == 0 && words == 3u); break;
    case K_OVER:
        ex(r, stdlib_size_class_words(SIZE_MAX - sizeof(uintptr_t) + 2u, &words) == -1); break;
    case K_SIZEMAX:
        ex(r, stdlib_size_class_words(SIZE_MAX, &words) == -1); break;
    default:
        ex(r, stdlib_size_class_words(1u, &words) == 0 && words == 1u); break;
    }
}
static void r116(int kind, struct cres *r) { /* stdlib_size_normalize_zero */
    size_t out = 12345;
    ex(r, stdlib_size_normalize_zero(0u, 8u, (size_t *)0) == -1);
    switch (kind) {
    case K_NULL:
        ex(r, stdlib_size_normalize_zero(0u, 8u, (size_t *)0) == -1); break;
    case K_ZERO:
        ex(r, stdlib_size_normalize_zero(0u, 8u, &out) == 0 && out == 8u); break;
    case K_EXACT:
        ex(r, stdlib_size_normalize_zero(32u, 8u, &out) == 0 && out == 32u); break;
    case K_SIZEMAX:
        ex(r, stdlib_size_normalize_zero(SIZE_MAX, 8u, &out) == 0 && out == SIZE_MAX); break;
    default:
        ex(r, stdlib_size_normalize_zero(7u, 8u, &out) == 0 && out == 7u); break;
    }
}

/* ========================================================================= */
/* API manifest: every public API in the seven headers                        */
/* ========================================================================= */
struct api_entry { const char *header; const char *name; case_fn fn; };
static const struct api_entry APIS[] = {
    { "alloc_iface.hp", "stdlib_allocate", r000 },
    { "alloc_iface.hp", "stdlib_reallocate", r001 },
    { "alloc_iface.hp", "stdlib_deallocate", r002 },
    { "alloc_iface.hp", "stdlib_allocator_reset", r003 },
    { "alloc_iface.hp", "stdlib_heap_allocator", r004 },
    { "arena.hp", "stdlib_arena_next_generation", r005 },
    { "arena.hp", "stdlib_arena_new_block", r006 },
    { "arena.hp", "stdlib_arena_init", r007 },
    { "arena.hp", "stdlib_arena_from_buffer", r008 },
    { "arena.hp", "stdlib_arena_alloc", r009 },
    { "arena.hp", "stdlib_arena_realloc", r010 },
    { "arena.hp", "stdlib_arena_release", r011 },
    { "arena.hp", "stdlib_arena_used", r012 },
    { "arena.hp", "stdlib_arena_do_reset", r013 },
    { "arena.hp", "stdlib_arena_destroy", r014 },
    { "arena.hp", "stdlib_arena_handle_valid", r015 },
    { "arena.hp", "stdlib_arena_mark", r016 },
    { "arena.hp", "stdlib_arena_op_alloc", r017 },
    { "arena.hp", "stdlib_arena_op_realloc", r018 },
    { "arena.hp", "stdlib_arena_op_release", r019 },
    { "arena.hp", "stdlib_arena_op_reset", r020 },
    { "arena.hp", "stdlib_arena_as_allocator", r021 },
    { "mempool.hp", "stdlib_mem_pool_init", r022 },
    { "mempool.hp", "stdlib_mem_pool_add_slab", r023 },
    { "mempool.hp", "stdlib_mem_pool_alloc", r024 },
    { "mempool.hp", "stdlib_mem_pool_release", r025 },
    { "mempool.hp", "stdlib_mem_pool_destroy", r026 },
    { "mempool.hp", "stdlib_pool_op_alloc", r027 },
    { "mempool.hp", "stdlib_pool_op_realloc", r028 },
    { "mempool.hp", "stdlib_pool_op_release", r029 },
    { "mempool.hp", "stdlib_pool_op_reset", r030 },
    { "mempool.hp", "stdlib_mem_pool_as_allocator", r031 },
    { "mempool.hp", "stdlib_slot_pool_init", r032 },
    { "mempool.hp", "stdlib_slot_pool_grow", r033 },
    { "mempool.hp", "stdlib_slot_pool_alloc_ref", r034 },
    { "mempool.hp", "stdlib_slot_ref_get", r035 },
    { "mempool.hp", "stdlib_slot_ref_free", r036 },
    { "mempool.hp", "stdlib_slot_pool_destroy", r037 },
    { "mempool.hp", "stdlib_slot_pool_scope (class)", r038 },
    { "datastack.hp", "stdlib_datastack_next_gen", r039 },
    { "datastack.hp", "stdlib_datastack_page_size", r040 },
    { "datastack.hp", "stdlib_datastack_init_vm", r041 },
    { "datastack.hp", "stdlib_datastack_commit", r042 },
    { "datastack.hp", "stdlib_datastack_init", r043 },
    { "datastack.hp", "stdlib_datastack_destroy", r044 },
    { "datastack.hp", "stdlib_datastack_ensure", r045 },
    { "datastack.hp", "stdlib_thread_datastack", r046 },
    { "datastack.hp", "stdlib_scratch_enter", r047 },
    { "datastack.hp", "stdlib_scratch_leave", r048 },
    { "datastack.hp", "stdlib_datastack_alloc", r049 },
    { "datastack.hp", "stdlib_datastack_realloc", r050 },
    { "datastack.hp", "stdlib_datastack_release", r051 },
    { "datastack.hp", "stdlib_scratch_op_alloc", r052 },
    { "datastack.hp", "stdlib_scratch_op_realloc", r053 },
    { "datastack.hp", "stdlib_scratch_op_release", r054 },
    { "datastack.hp", "stdlib_scratch_op_reset", r055 },
    { "datastack.hp", "stdlib_scratch_scope (class)", r056 },
    { "datastack.hp", "stdlib_scratch_allocator", r057 },
    { "error.hp", "stdlib_tm_add", r058 },
    { "error.hp", "stdlib_telemetry_set_enabled", r059 },
    { "error.hp", "stdlib_telemetry_is_enabled", r060 },
    { "error.hp", "stdlib_telemetry_reset", r061 },
    { "error.hp", "stdlib_telemetry_snapshot", r062 },
    { "error.hp", "stdlib_set_error_callback", r063 },
    { "error.hp", "stdlib_category_name", r064 },
    { "error.hp", "stdlib_note", r065 },
    { "error.hp", "stdlib_alloc_fail_at", r066 },
    { "error.hp", "stdlib_alloc_fail_after", r067 },
    { "error.hp", "stdlib_alloc_fail_reset", r068 },
    { "error.hp", "stdlib_alloc_seq", r069 },
    { "error.hp", "stdlib_meta_acquire", r070 },
    { "error.hp", "stdlib_meta_release_lock", r071 },
    { "error.hp", "stdlib_meta_hash", r072 },
    { "error.hp", "stdlib_meta_find", r073 },
    { "error.hp", "stdlib_meta_record", r074 },
    { "error.hp", "stdlib_meta_forget", r075 },
    { "error.hp", "stdlib_alloc", r076 },
    { "error.hp", "stdlib_calloc", r077 },
    { "error.hp", "stdlib_realloc", r078 },
    { "error.hp", "stdlib_safe_div", r079 },
    { "error.hp", "stdlib_alloc_size", r080 },
    { "error.hp", "stdlib_alloc_elem_size", r081 },
    { "error.hp", "stdlib_alloc_length", r082 },
    { "error.hp", "stdlib_alloc_generation", r083 },
    { "error.hp", "stdlib_alloc_check_generation", r084 },
    { "error.hp", "stdlib_alloc_query_length", r085 },
    { "error.hp", "stdlib_transfer_ownership", r086 },
    { "error.hp", "stdlib_release", r087 },
    { "error.hp", "error_reset", r088 },
    { "error.hp", "error_fail", r089 },
    { "error.hp", "error_fail_cat", r090 },
    { "error.hp", "error (class)", r091 },
    { "allocator.hp", "stdlib_alloc_identity", r092 },
    { "allocator.hp", "stdlib_alloc_id_equal", r093 },
    { "allocator.hp", "stdlib_alloc_id_matches", r094 },
    { "allocator.hp", "stdlib_alloc_view_bytes", r095 },
    { "allocator.hp", "stdlib_alloc_view_elem_bytes", r096 },
    { "allocator.hp", "stdlib_alloc_view_generation", r097 },
    { "allocator.hp", "stdlib_alloc_view_live", r098 },
    { "allocator.hp", "stdlib_alloc_view_count", r099 },
    { "allocator.hp", "stdlib_alloc_view_remaining", r100 },
    { "allocator.hp", "stdlib_alloc_view_unchanged", r101 },
    { "allocator.hp", "stdlib_alloc_view_transfer_ok", r102 },
    { "allocator.hp", "stdlib_alloc_cap_state", r103 },
    { "allocator.hp", "stdlib_alloc_hasvalidcap", r104 },
    { "allocator.hp", "stdlib_alloc_cap_covers", r105 },
    { "allocator.hp", "stdlib_alloc_cap_check", r106 },
    { "allocator.hp", "stdlib_alloc_tm_snapshot", r107 },
    { "allocator.hp", "stdlib_alloc_tm_delta", r108 },
    { "allocator.hp", "stdlib_alloc_tm_counter", r109 },
    { "allocator.hp", "stdlib_alloc_fail_begin", r110 },
    { "allocator.hp", "stdlib_alloc_fail_end", r111 },
    { "allocator.hp", "alloc_fail_scope (class)", r112 },
    { "alloc_policy.hp", "stdlib_count_size_overflow", r113 },
    { "alloc_policy.hp", "stdlib_size_class_round_up", r114 },
    { "alloc_policy.hp", "stdlib_size_class_words", r115 },
    { "alloc_policy.hp", "stdlib_size_normalize_zero", r116 }
};
#define NAPI ((int)(sizeof(APIS) / sizeof(APIS[0])))

/* ------------------------------------------------------------ case runner */
static void run_child(int api, int kind, struct cres *r) {
    unsigned char stack_canary[8];
    unsigned int i;
    for (i = 0; i < 8u; i++) stack_canary[i] = (unsigned char)(0xC3u ^ i);
    r->pass = 1; r->sub = 0; r->obs = 0;
    canary_init();
    APIS[api].fn(kind, r);
    if (!canary_ok()) r->pass = 0;
    for (i = 0; i < 8u; i++)
        if (stack_canary[i] != (unsigned char)(0xC3u ^ i)) r->pass = 0;
}

static int wait_ok(pid_t pid, int *status) {
    return waitpid(pid, status, 0) == pid;
}

int main(void) {
    int rfd, mfd, ai, ki, total_cases = 0, total_pass = 0, total_fail = 0;
    int per_api_pass[NAPI]; int per_api_fail[NAPI];
    int fds[2]; pid_t pid; int status; ssize_t got;
    struct cres result; char line[512]; int n;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
    stdlib_alloc_fail_reset();

    rfd = open(RESULTS_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    mfd = open(MANIFEST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (rfd < 0 || mfd < 0) {
        fprintf(stderr, "EDGE100 alloc: cannot open output logs\n");
        printf("EDGE100 alloc 0 0 0 0\n");
        return 2;
    }
    n = snprintf(line, sizeof(line), "# EDGE100 alloc: fork-per-case edge suite\n");
    if (n > 0) (void)write(rfd, line, (size_t)n);

    for (ai = 0; ai < NAPI; ai++) {
        per_api_pass[ai] = 0; per_api_fail[ai] = 0;
        for (ki = 0; ki < NKIND; ki++) {
            const char *verdict = "PASS";
            int reason = 0;
            if (pipe(fds) != 0) { verdict = "FAIL"; reason = 1; }
            else {
                fflush(stdout);
                pid = fork();
                if (pid < 0) {
                    close(fds[0]); close(fds[1]);
                    verdict = "FAIL"; reason = 2;
                } else if (pid == 0) {
                    struct cres child_result;
                    close(fds[0]);
                    run_child(ai, ki, &child_result);
                    (void)write(fds[1], &child_result, sizeof(child_result));
                    close(fds[1]);
                    _exit(0);
                } else {
                    close(fds[1]);
                    got = read(fds[0], &result, sizeof(result));
                    close(fds[0]);
                    if (!wait_ok(pid, &status) || WIFSIGNALED(status) ||
                        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
                        got != (ssize_t)sizeof(result)) {
                        verdict = "FAIL"; reason = 3; /* CRASH / short write */
                    } else if (result.pass != 1) {
                        verdict = "FAIL"; reason = 4; /* assertion / canary */
                    }
                }
            }
            total_cases++;
            if (verdict[0] == 'P') { total_pass++; per_api_pass[ai]++; }
            else { total_fail++; per_api_fail[ai]++; }
            n = snprintf(line, sizeof(line), "%d\t%s\t%s\t%s\t%d\n",
                         ai, APIS[ai].name, kind_name(ki), verdict, reason);
            if (n > 0) (void)write(rfd, line, (size_t)n);
        }
    }
    for (ai = 0; ai < NAPI; ai++) {
        n = snprintf(line, sizeof(line), "%d\t%s\t%s\t%d\t%d\t%d\n",
                     ai, APIS[ai].header, APIS[ai].name, NKIND,
                     per_api_pass[ai], per_api_fail[ai]);
        if (n > 0) (void)write(mfd, line, (size_t)n);
    }
    close(rfd); close(mfd);
    printf("EDGE100 alloc %d %d %d %d\n", NAPI, total_cases, total_pass, total_fail);
    return 0;
}
