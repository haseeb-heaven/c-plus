/* RED-BLUE20 agent 1/20 (ATTACKER): modern allocator red-team harness.
 *
 * Targets (modern layer only):
 *   stdlib/alloc_iface.hp, stdlib/arena.hp,
 *   stdlib/mempool.hp,     stdlib/datastack.hp
 *
 * METHOD
 *   >=3000 deterministic adversarial cases, fixed seed, one case per forked
 *   child.  Child reports a 3-int classification over a pipe; a child that
 *   dies by signal / fatal exit / short write is a CRASH.  Canaries and an
 *   mmap PROT_NONE guard page detect silent corruption.  Families rotate by
 *   case index so coverage is deterministic and independent of timing.
 *
 * VERDICTS
 *   REFUSED        - target refused exactly as documented / canary intact
 *   UNEXPECTED_OK  - target accepted hostile input it promises to refuse
 *   CRASH          - child died by signal or fatal exit
 *   CORRUPT        - canary / alignment / documented guarantee violated
 *
 * BUILD (from repository root)
 *   ./build/c+ build tests/redblue20/red_alloc/harness.cp \
 *     --default --sanitize=none -I. \
 *     -o tests/redblue20/red_alloc/out/harness \
 *     -- build/libc/libcplus_hardened.a
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/mempool.hp"
#include "stdlib/datastack.hp"

#define N_CASES   3200
#define RNG_SEED  0xA11C0DE5EED5A11CULL
#define LOG_PATH  "tests/redblue20/red_alloc/out/scenarios.log"

#define CLS_REFUSED       0
#define CLS_UNEXPECTED_OK 1
#define CLS_CRASH         2
#define CLS_CORRUPT       3

struct case_result {
    int cls;
    int sub;
    int got;
};

struct case_spec {
    int family;
    unsigned long long a;
    unsigned long long b;
    unsigned long long c;
};

enum {
    F_ALLOC_INVALID = 0,
    F_ALIGN_OVER,
    F_ARENA_INVALID,
    F_ARENA_STALE,
    F_ARENA_GUARD,
    F_ARENA_REALLOC_TOP,
    F_ARENA_REALLOC_FORGED,
    F_POOL_FOREIGN,
    F_POOL_DOUBLEFREE,
    F_POOL_SLAB_OVF,
    F_POOL_SLOT_OVF,
    F_SLOTREF_STALE,
    F_SLOTREF_GROW,
    F_DS_INVALID,
    F_DS_VM_EXHAUST,
    F_DS_REALLOC_FORGED,
    F_DS_SCOPE,
    F_DS_RELEASE_FORGED,
    F_DS_STALE_MARK,
    F_ALLOC_REALIGN,
    F_COUNT
};

/* ---- deterministic PRNG (splitmix64) ----------------------------------- */
static unsigned long long g_rng;

static unsigned long long next_rand(void) {
    unsigned long long z;
    z = (g_rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static struct case_spec gen_spec(int index) {
    struct case_spec s;
    s.family = index % F_COUNT;
    s.a = next_rand();
    s.b = next_rand();
    s.c = next_rand();
    return s;
}

/* ---- classification helpers -------------------------------------------- */
static void setres(struct case_result *r, int cls, int sub, int got) {
    r->cls = cls; r->sub = sub; r->got = got;
}

static void expect_refuse(struct case_result *r, int accepted, int sub,
                          int got) {
    setres(r, accepted ? CLS_UNEXPECTED_OK : CLS_REFUSED, sub, got);
}

static void expect_clean(struct case_result *r, int violated, int sub,
                         int got) {
    setres(r, violated ? CLS_CORRUPT : CLS_REFUSED, sub, got);
}

static int all_bytes(const void *p, size_t n, unsigned char v) {
    const unsigned char *q = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i) if (q[i] != v) return 0;
    return 1;
}

/* ======================================================================== */
/* ATTACK FAMILIES                                                           */
/* ======================================================================== */

/* 0: invalid size / alignment / NULL vtable dispatch must all refuse. */
static void f_alloc_invalid(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_allocator A = stdlib_heap_allocator();
    void *p = (void *)0;
    int v = (int)(s->a % 7ULL);
    if (v == 0)      p = stdlib_allocate((const stdlib_allocator *)0, 0, 16, &e);
    else if (v == 1) p = stdlib_allocate((const stdlib_allocator *)0, SIZE_MAX, 8, &e);
    else if (v == 2) p = stdlib_allocate((const stdlib_allocator *)0, 64, 3, &e);
    else if (v == 3) p = stdlib_allocate((const stdlib_allocator *)0, 64, 5, &e);
    else if (v == 4) p = stdlib_reallocate((const stdlib_allocator *)0, 0, 0, 0, 16, &e);
    else if (v == 5) p = stdlib_allocate(&A, 32, 6, &e);
    else             p = stdlib_reallocate(&A, 0, 0, 64, 24, &e);
    if (p != (void *)0) stdlib_deallocate((const stdlib_allocator *)0, p, 64);
    expect_refuse(r, p != (void *)0, v, p != (void *)0);
}

/* 1: FINDING F-ALIGN-01 -- default allocator silently ignores over-alignment. */
static void f_align_over(struct case_spec *s, struct case_result *r) {
    error e = error();
    size_t align = (size_t)1 << (6u + (unsigned)(s->a % 11ULL)); /* 64..65536 */
    int k, bad = 0, got = 0;
    for (k = 0; k < 8; ++k) {
        void *p = stdlib_allocate((const stdlib_allocator *)0, 48, align, &e);
        if (p == (void *)0) continue;
        ++got;
        if (((uintptr_t)p & (uintptr_t)(align - 1u)) != 0) ++bad;
        stdlib_release(p);
    }
    /* Only a genuine alignment violation counts as CORRUPT. */
    expect_clean(r, bad > 0, (int)(align >> 6), bad);
    (void)s;
}

/* 2: arena invalid arguments must refuse, including SIZE_MAX block size. */
static void f_arena_invalid(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_arena a;
    void *p = (void *)0;
    int v = (int)(s->a % 8ULL);
    int rc = STDLIB_OK;
    if (v == 0)      rc = stdlib_arena_init(&a, SIZE_MAX, &e);
    else if (v == 1) rc = stdlib_arena_init((stdlib_arena *)0, 128, &e);
    else if (v == 2) p = stdlib_arena_alloc((stdlib_arena *)0, 32, 16, &e);
    else if (v == 3) {
        stdlib_arena_init(&a, 128, &e);
        p = stdlib_arena_alloc(&a, 0, 16, &e);
    } else if (v == 4) {
        stdlib_arena_init(&a, 128, &e);
        p = stdlib_arena_alloc(&a, 32, 3, &e);
    } else if (v == 5) {
        stdlib_arena_init(&a, 128, &e);
        p = stdlib_arena_alloc(&a, 32, 5, &e);
    } else if (v == 6) {
        stdlib_arena_init(&a, 128, &e);
        p = stdlib_arena_alloc(&a, SIZE_MAX - 8u, 16, &e);
    } else {
        stdlib_arena_init(&a, 128, &e);
        p = stdlib_arena_realloc(&a, 0, 0, 0, 16, &e);
    }
    expect_refuse(r, (v <= 1 && rc == STDLIB_OK) || (v > 1 && p != (void *)0),
                  v, v <= 1 ? rc : (p != (void *)0));
}

/* 3: stale arena handles after reset/destroy must be rejected. */
static void f_arena_stale(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_arena a;
    stdlib_arena_handle h;
    int v = (int)(s->a % 4ULL);
    void *after;
    int stale_ok, after_ok = 0;
    stdlib_arena_init(&a, 128, &e);
    h = stdlib_arena_mark(&a);
    (void)stdlib_arena_alloc(&a, 32, 16, &e);
    if (v == 0)      stdlib_arena_do_reset(&a, &e);
    else if (v == 1) stdlib_arena_destroy(&a, &e);
    else if (v == 2) {
        stdlib_arena_do_reset(&a, &e);
        stdlib_arena_init(&a, 128, &e);
    } else {
        stdlib_arena_destroy(&a, &e);
        stdlib_arena_init(&a, 128, &e);
    }
    stale_ok = stdlib_arena_handle_valid(&h);
    after = stdlib_arena_alloc(&a, 8, 16, &e);
    if (v == 1 && after != (void *)0) after_ok = 1;
    expect_refuse(r, stale_ok || after_ok, v, (stale_ok ? 1 : 0) |
                                                (after_ok ? 2 : 0));
}

/* 4: caller buffer + mmap PROT_NONE guard page: no write past the buffer. */
static void f_arena_guard(struct case_spec *s, struct case_result *r) {
    error e = error();
    long ps = sysconf(_SC_PAGESIZE);
    size_t page = (size_t)ps;
    unsigned char *region;
    stdlib_arena a;
    unsigned char *p;
    unsigned char *canary;
    void *q;
    int ok;
    if (ps <= 0) { setres(r, CLS_REFUSED, 0, 0); return; }
    region = (unsigned char *)mmap(0, page * 2u, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == (unsigned char *)MAP_FAILED) { setres(r, CLS_REFUSED, 1, 0); return; }
    if (mprotect(region + page, page, PROT_NONE) != 0) {
        munmap(region, page * 2u); setres(r, CLS_REFUSED, 2, 0); return;
    }
    canary = region + page - 8u;
    memset(canary, 0x5A, 8u);
    if (stdlib_arena_from_buffer(&a, region, page, &e) != STDLIB_OK) {
        munmap(region, page * 2u); setres(r, CLS_REFUSED, 3, 0); return;
    }
    p = (unsigned char *)stdlib_arena_alloc(&a, page - 64u, 16, &e);
    if (p != (void *)0) memset(p, 0x11, page - 64u);
    q = stdlib_arena_alloc(&a, 128u + (size_t)(s->b % 64ULL), 16, &e);
    ok = all_bytes(canary, 8u, 0x5A);
    expect_clean(r, !ok, q != (void *)0 ? 1 : 0, ok ? 0 : 1);
    munmap(region, page * 2u);
}

/* 5: valid top-of-stack realloc grow/shrink keeps neighbour canary intact. */
static void f_arena_realloc_top(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_arena a;
    unsigned char *base;
    unsigned char *nb;
    void *p;
    void *q;
    int v = (int)(s->a % 4ULL);
    stdlib_arena_init(&a, 512, &e);
    base = (unsigned char *)stdlib_arena_alloc(&a, 64, 16, &e);
    memset(base, 0xC3, 64u);
    p = stdlib_arena_alloc(&a, 64, 16, &e);
    q = stdlib_arena_realloc(&a, p, 64, v < 2 ? 128u : 32u, 16, &e);
    nb = (unsigned char *)stdlib_arena_alloc(&a, 32, 16, &e);
    (void)nb;
    expect_clean(r, !all_bytes(base, 64u, 0xC3), v,
                 all_bytes(base, 64u, 0xC3) ? 0 : 1);
    (void)q;
}

/* 6: FINDING F-ARENA-02 -- forged old_size aliases a non-top block with the
 *    top cursor, or drives an unbounded memcpy. */
static void f_arena_realloc_forged(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_arena a;
    unsigned char *p;
    unsigned char *q;
    unsigned char *n;
    int v = (int)(s->a % 4ULL);
    int changed = 0, i;
    stdlib_arena_init(&a, 512, &e);
    p = (unsigned char *)stdlib_arena_alloc(&a, 64, 16, &e);
    q = (unsigned char *)stdlib_arena_alloc(&a, 64, 16, &e);
    for (i = 0; i < 16; ++i) q[i] = 0xA5;
    if (v < 3) {
        /* cursor - old_size == p although p is not the last allocation */
        size_t forged = (size_t)(q - p) + 64u + (size_t)(s->b % 8ULL);
        (void)stdlib_arena_realloc(&a, p, forged, 16, 16, &e);
        n = (unsigned char *)stdlib_arena_alloc(&a, 64, 16, &e);
        for (i = 0; i < 16; ++i) if (q[i] != 0xA5) ++changed;
        expect_clean(r, changed > 0, v, changed);
        (void)n;
    } else {
        /* huge old_size: top underflows, non-top path memcpy's old_size bytes */
        (void)stdlib_arena_realloc(&a, p, (size_t)1 << 50, 32, 16, &e);
        setres(r, CLS_REFUSED, v, 0); /* survives only if library validates */
    }
}

/* 7: pool foreign-pointer free: other pool / stack / managed / wild. */
static void f_pool_foreign(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_mem_pool pa, pb;
    void *sa;
    void *stack_obj;
    int v = (int)(s->a % 4ULL);
    void *bad;
    stdlib_mem_pool_init(&pa, 64, 4, &e);
    stdlib_mem_pool_init(&pb, 64, 4, &e);
    sa = stdlib_mem_pool_alloc(&pa, &e);
    (void)stdlib_mem_pool_alloc(&pb, &e);
    stack_obj = (void *)&stack_obj;
    if (v == 0)      bad = sa;                 /* foreign: owned by pa, freed via pb? */
    else if (v == 1) bad = stack_obj;          /* untracked stack address */
    else if (v == 2) bad = (void *)(uintptr_t)0x1000u; /* wild low address */
    else             bad = stdlib_alloc(64);   /* managed heap, not a slot */
    if (v == 0) stdlib_mem_pool_release(&pb, bad, &e);
    else        stdlib_mem_pool_release(&pa, bad, &e);
    expect_refuse(r, e.failed_now() == 0, v, e.code());
    if (v == 3) stdlib_release(bad);
    stdlib_mem_pool_destroy(&pa, &e);
    stdlib_mem_pool_destroy(&pb, &e);
}

/* 8: double free / misaligned slot free must be refused. */
static void f_pool_doublefree(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_mem_pool p;
    void *slot;
    int v = (int)(s->a % 3ULL);
    stdlib_mem_pool_init(&p, 64, 4, &e);
    slot = stdlib_mem_pool_alloc(&p, &e);
    stdlib_mem_pool_release(&p, slot, &e);
    error_reset(&e);
    if (v == 0)      stdlib_mem_pool_release(&p, slot, &e); /* double free */
    else if (v == 1) stdlib_mem_pool_release(&p, (unsigned char *)slot + 1u, &e);
    else {
        void *other = stdlib_mem_pool_alloc(&p, &e);
        stdlib_mem_pool_release(&p, slot, &e);
        stdlib_mem_pool_release(&p, other, &e);
        stdlib_mem_pool_release(&p, slot, &e); /* second re-release */
    }
    expect_refuse(r, e.failed_now() == 0, v, e.code());
    stdlib_mem_pool_destroy(&p, &e);
}

/* 9: FINDING F-POOL-01 -- raw slab-size overflow wraps the bounds check. */
static void f_pool_slab_ovf(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_mem_pool p;
    size_t per = (size_t)0x78787878787878cULL; /* slot_size=1, stride=33 */
    (void)s;
    stdlib_mem_pool_init(&p, 1, per, &e);
    /* A correct init/grow path must refuse; any return here is a survive. */
    (void)stdlib_mem_pool_alloc(&p, &e);
    setres(r, CLS_REFUSED, 0, 0);
}

/* 10: FINDING F-POOL-02 -- slot_pool_init slot_size*capacity overflow. */
static void f_pool_slot_ovf(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_slot_pool sp;
    stdlib_slot_ref ref;
    size_t ss;
    int v = (int)(s->a % 2ULL);
    if (v == 0) ss = ((size_t)1 << 61) + 1u; /* wraps to 8 bytes at cap=8 */
    else        ss = ((size_t)1 << 62) + 1u; /* wraps to 8 bytes at cap=8 */
    (void)stdlib_slot_pool_init(&sp, ss, 0, &e);
    (void)stdlib_slot_pool_alloc_ref(&sp, &ref, &e);
    setres(r, CLS_REFUSED, v, 0);
}

/* 11: slot_ref stale get/free after free and after pool destroy. */
static void f_slotref_stale(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_slot_pool sp;
    stdlib_slot_ref ref, copy;
    void *p;
    int v = (int)(s->a % 4ULL);
    int accepted = 0;
    stdlib_slot_pool_init(&sp, 32, 4, &e);
    (void)stdlib_slot_pool_alloc_ref(&sp, &ref, &e);
    copy = ref;
    if (v == 0 || v == 1) {
        stdlib_slot_ref_free(&ref, &e);
        error_reset(&e);
        p = stdlib_slot_ref_get(&copy, &e);
        if (p != (void *)0) accepted = 1;
        if (v == 1) {
            error_reset(&e);
            stdlib_slot_ref_free(&copy, &e);
            if (e.failed_now() == 0) accepted = 1;
        }
    } else {
        stdlib_slot_pool_destroy(&sp, &e);
        error_reset(&e);
        p = stdlib_slot_ref_get(&copy, &e);
        if (p != (void *)0) accepted = 1;
    }
    expect_refuse(r, accepted, v, accepted);
}

/* 12: legitimate slot-pool growth keeps handles and canary valid. */
static void f_slotref_grow(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_slot_pool sp;
    stdlib_slot_ref r0, ri;
    unsigned char *p0;
    int i, ok;
    (void)s;
    stdlib_slot_pool_init(&sp, 16, 2, &e);
    (void)stdlib_slot_pool_alloc_ref(&sp, &r0, &e);
    p0 = (unsigned char *)stdlib_slot_ref_get(&r0, &e);
    if (p0 != (void *)0) memset(p0, 0x77, 16u);
    for (i = 0; i < 24; ++i) (void)stdlib_slot_pool_alloc_ref(&sp, &ri, &e);
    p0 = (unsigned char *)stdlib_slot_ref_get(&r0, &e);
    ok = p0 != (void *)0 && all_bytes(p0, 16u, 0x77);
    expect_clean(r, !ok, 0, ok ? 0 : 1);
    stdlib_slot_pool_destroy(&sp, &e);
}

/* 13: datastack invalid arguments and zero-size init must refuse. */
static void f_ds_invalid(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_datastack ds;
    void *p = (void *)0;
    int rc = STDLIB_OK;
    int v = (int)(s->a % 7ULL);
    if (v == 0)      rc = stdlib_datastack_init_vm(&ds, 0, &e);
    else if (v == 1) rc = stdlib_datastack_init(&ds, 0, &e);
    else if (v == 2) p = stdlib_datastack_alloc((stdlib_datastack *)0, 32, 16, &e);
    else if (v == 3) {
        stdlib_datastack_init(&ds, 4096, &e);
        p = stdlib_datastack_alloc(&ds, 0, 16, &e);
    } else if (v == 4) {
        stdlib_datastack_init(&ds, 4096, &e);
        p = stdlib_datastack_alloc(&ds, 32, 3, &e);
    } else if (v == 5) {
        stdlib_datastack_init(&ds, 4096, &e);
        p = stdlib_datastack_alloc(&ds, 32, 5, &e);
    } else {
        stdlib_datastack_init(&ds, 4096, &e);
        p = stdlib_datastack_realloc(&ds, 0, 0, 0, 16, &e);
    }
    expect_refuse(r, (v <= 1 && rc != STDLIB_FAIL) ||
                     (v > 1 && p != (void *)0), v, v <= 1 ? rc : (p != 0));
    if (v > 1 && ds.base != (unsigned char *)0) stdlib_datastack_destroy(&ds, &e);
}

/* 14: VM reserve exhaustion / commit beyond capacity must refuse. */
static void f_ds_vm_exhaust(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_datastack ds;
    int v = (int)(s->a % 3ULL);
    int rc;
    if (v == 0)      rc = stdlib_datastack_init_vm(&ds, SIZE_MAX - 4095u, &e);
    else if (v == 1) rc = stdlib_datastack_init(&ds, SIZE_MAX, &e);
    else {
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) { setres(r, CLS_REFUSED, v, 0); return; }
        rc = stdlib_datastack_init_vm(&ds, (size_t)ps * 4u, &e);
        if (rc == STDLIB_OK) {
            rc = stdlib_datastack_commit(&ds, ds.capacity + 1u, &e);
            stdlib_datastack_destroy(&ds, &e);
            expect_refuse(r, rc == STDLIB_OK, v, rc);
            return;
        }
    }
    expect_refuse(r, rc == STDLIB_OK, v, rc);
}

/* 15: FINDING F-DS-01 -- forged old_size on non-top realloc memcpy's it. */
static void f_ds_realloc_forged(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_datastack ds;
    unsigned char *p;
    void *q;
    int v = (int)(s->a % 2ULL);
    (void)s;
    if (stdlib_datastack_init(&ds, 16384, &e) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 0, 0); return;
    }
    p = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 16, &e);
    if (p == (void *)0) { setres(r, CLS_REFUSED, 1, 0); return; }
    memset(p, 0x3C, 64u);
    if (v == 0) q = stdlib_datastack_realloc(&ds, p, (size_t)1 << 40, 32, 16, &e);
    else        q = stdlib_datastack_realloc(&ds, p, (size_t)1 << 50, 32, 16, &e);
    setres(r, CLS_REFUSED, v, q != (void *)0); /* survives only if validated */
}

/* 16: nested scratch scopes, including out-of-order leave; live prefix must
 *     keep its canary. */
static void f_ds_scope(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    stdlib_scratch_mark m1, m2;
    unsigned char *p1;
    unsigned char *p2;
    unsigned char *p3;
    int v = (int)(s->a % 3ULL);
    int ok;
    if (ds == (stdlib_datastack *)0) { setres(r, CLS_REFUSED, 0, 0); return; }
    m1 = stdlib_scratch_enter(ds);
    p1 = (unsigned char *)stdlib_datastack_alloc(ds, 64, 16, &e);
    if (p1 != (void *)0) memset(p1, 0x91, 64u);
    m2 = stdlib_scratch_enter(ds);
    p2 = (unsigned char *)stdlib_datastack_alloc(ds, 64, 16, &e);
    if (p2 != (void *)0) memset(p2, 0x82, 64u);
    if (v == 0)      { stdlib_scratch_leave(ds, m1); stdlib_scratch_leave(ds, m2); }
    else if (v == 1) { stdlib_scratch_leave(ds, m2); stdlib_scratch_leave(ds, m1); }
    else             { stdlib_scratch_leave(ds, m1); }
    p3 = (unsigned char *)stdlib_datastack_alloc(ds, 32, 16, &e);
    (void)p3; (void)p2;
    ok = p1 != (void *)0 && all_bytes(p1, 64u, 0x91);
    expect_clean(r, !ok, v, ok ? 0 : 1);
}

/* 17: FINDING F-DS-02 -- forged release size rewinds top over a live block. */
static void f_ds_release_forged(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_datastack ds;
    unsigned char *p1;
    unsigned char *p2;
    unsigned char *n;
    int changed = 0, i;
    (void)s;
    if (stdlib_datastack_init(&ds, 8192, &e) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 0, 0); return;
    }
    p1 = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 16, &e);
    p2 = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 16, &e);
    for (i = 0; i < 16; ++i) p1[i] = 0xE7;
    (void)p2;
    stdlib_datastack_release(&ds, p1, 128, &e); /* forged: rewinds past p1 */
    n = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 16, &e);
    for (i = 0; i < 16; ++i) if (p1[i] != 0xE7) ++changed;
    expect_clean(r, changed > 0, 0, changed);
    stdlib_datastack_destroy(&ds, &e);
}

/* 18: stale scratch mark after destroy must be ignored (generation guard). */
static void f_ds_stale_mark(struct case_spec *s, struct case_result *r) {
    error e = error();
    stdlib_datastack ds;
    stdlib_scratch_mark m;
    (void)s;
    if (stdlib_datastack_init(&ds, 4096, &e) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 0, 0); return;
    }
    m = stdlib_scratch_enter(&ds);
    (void)stdlib_datastack_alloc(&ds, 64, 16, &e);
    stdlib_datastack_destroy(&ds, &e);
    stdlib_scratch_leave(&ds, m); /* must be a no-op, not a write to a dead ds */
    setres(r, CLS_REFUSED, 0, 0);
}

/* 19: FINDING F-ALIGN-01b -- default realloc also ignores over-alignment. */
static void f_alloc_realign(struct case_spec *s, struct case_result *r) {
    error e = error();
    size_t align = (size_t)1 << (8u + (unsigned)(s->a % 9ULL)); /* 256..65536 */
    int k, bad = 0, got = 0;
    for (k = 0; k < 8; ++k) {
        void *p = stdlib_allocate((const stdlib_allocator *)0, 48, 16, &e);
        void *q;
        if (p == (void *)0) continue;
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 48, 64, align, &e);
        if (q == (void *)0) continue;
        ++got;
        if (((uintptr_t)q & (uintptr_t)(align - 1u)) != 0) ++bad;
        stdlib_release(q);
    }
    expect_clean(r, bad > 0, (int)(align >> 8), bad);
}

/* ---- dispatch ----------------------------------------------------------- */
static void run_attack(struct case_spec *s, struct case_result *r) {
    r->cls = CLS_REFUSED; r->sub = -1; r->got = 0;
    switch (s->family) {
        case F_ALLOC_INVALID:        f_alloc_invalid(s, r); break;
        case F_ALIGN_OVER:           f_align_over(s, r); break;
        case F_ARENA_INVALID:        f_arena_invalid(s, r); break;
        case F_ARENA_STALE:          f_arena_stale(s, r); break;
        case F_ARENA_GUARD:          f_arena_guard(s, r); break;
        case F_ARENA_REALLOC_TOP:    f_arena_realloc_top(s, r); break;
        case F_ARENA_REALLOC_FORGED: f_arena_realloc_forged(s, r); break;
        case F_POOL_FOREIGN:         f_pool_foreign(s, r); break;
        case F_POOL_DOUBLEFREE:      f_pool_doublefree(s, r); break;
        case F_POOL_SLAB_OVF:        f_pool_slab_ovf(s, r); break;
        case F_POOL_SLOT_OVF:        f_pool_slot_ovf(s, r); break;
        case F_SLOTREF_STALE:        f_slotref_stale(s, r); break;
        case F_SLOTREF_GROW:         f_slotref_grow(s, r); break;
        case F_DS_INVALID:           f_ds_invalid(s, r); break;
        case F_DS_VM_EXHAUST:        f_ds_vm_exhaust(s, r); break;
        case F_DS_REALLOC_FORGED:    f_ds_realloc_forged(s, r); break;
        case F_DS_SCOPE:             f_ds_scope(s, r); break;
        case F_DS_RELEASE_FORGED:    f_ds_release_forged(s, r); break;
        case F_DS_STALE_MARK:        f_ds_stale_mark(s, r); break;
        case F_ALLOC_REALIGN:        f_alloc_realign(s, r); break;
        default:                     setres(r, CLS_CRASH, -2, 0); break;
    }
}

static const char *class_name(int cls) {
    if (cls == CLS_UNEXPECTED_OK) return "UNEXPECTED_OK";
    if (cls == CLS_CRASH)         return "CRASH";
    if (cls == CLS_CORRUPT)       return "CORRUPT";
    return "REFUSED";
}

static int classify_child(int waited_ok, int status, ssize_t got,
                          struct case_result *res) {
    if (!waited_ok || WIFSIGNALED(status) || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || got != (ssize_t)sizeof(*res))
        return CLS_CRASH;
    if (res->cls < CLS_REFUSED || res->cls > CLS_CORRUPT) return CLS_CRASH;
    return res->cls;
}

static int selfcheck(void) {
    struct case_result r = {CLS_REFUSED, 0, 0};
    int status = 0, passed = 0;
    r.cls = CLS_UNEXPECTED_OK;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_UNEXPECTED_OK) passed++;
    r.cls = CLS_REFUSED;
    if (classify_child(1, (7 << 8), (ssize_t)sizeof(r), &r) == CLS_CRASH) passed++;
    if (classify_child(1, 11, (ssize_t)sizeof(r), &r) == CLS_CRASH) passed++;
    r.cls = CLS_CORRUPT;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CORRUPT) passed++;
    r.cls = 99;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CRASH) passed++;
    printf("SELFCHECK %d 5\n", passed);
    return passed == 5 ? 0 : 1;
}

int main(void) {
    const char *sc = getenv("RED_ALLOC_SELFCHECK");
    if (sc != (const char *)0 && strcmp(sc, "1") == 0) return selfcheck();
    int logfd, line_len, i, counts[4], fds[2], status = 0;
    ssize_t got;
    pid_t pid;
    char line[512];
    struct case_spec spec;
    struct case_result result;

    for (i = 0; i < 4; ++i) counts[i] = 0;
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
    g_rng = RNG_SEED;

    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) {
        fprintf(stderr, "cannot open %s\n", LOG_PATH);
        printf("RED20 alloc 0 0 0 0 0\n");
        return 2;
    }
    line_len = snprintf(line, sizeof(line),
        "# red_alloc seed=%016llx cases=%d families=%d\n",
        RNG_SEED, N_CASES, F_COUNT);
    if (line_len > 0) (void)write(logfd, line, (size_t)line_len);

    for (i = 0; i < N_CASES; ++i) {
        spec = gen_spec(i);
        if (pipe(fds) != 0) { fprintf(stderr, "pipe failed at %d\n", i); break; }
        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed at %d\n", i);
            close(fds[0]); close(fds[1]); break;
        }
        if (pid == 0) {
            struct case_result child_result;
            close(fds[0]);
            run_attack(&spec, &child_result);
            (void)write(fds[1], &child_result, sizeof(child_result));
            close(fds[1]);
            _exit(0);
        }
        close(fds[1]);
        got = read(fds[0], &result, sizeof(result));
        close(fds[0]);
        {
            int cls = classify_child(waitpid(pid, &status, 0) == pid, status,
                                     got, &result);
            counts[cls]++;
            line_len = snprintf(line, sizeof(line),
                "%d %d %llu %llu %llu %s sub=%d got=%d cls=%d\n",
                i, spec.family, spec.a, spec.b, spec.c, class_name(cls),
                cls == CLS_CRASH ? -1 : result.sub,
                cls == CLS_CRASH ? -1 : result.got, cls);
            if (line_len > 0) (void)write(logfd, line, (size_t)line_len);
        }
    }
    line_len = snprintf(line, sizeof(line),
        "# SUMMARY cases=%d refused=%d crashes=%d corrupt=%d unexpected_ok=%d\n",
        N_CASES, counts[CLS_REFUSED], counts[CLS_CRASH],
        counts[CLS_CORRUPT], counts[CLS_UNEXPECTED_OK]);
    if (line_len > 0) (void)write(logfd, line, (size_t)line_len);
    close(logfd);

    printf("RED20 alloc %d %d %d %d %d\n",
           N_CASES, counts[CLS_CRASH], counts[CLS_CORRUPT],
           counts[CLS_UNEXPECTED_OK], counts[CLS_REFUSED]);
    return 0;
}
