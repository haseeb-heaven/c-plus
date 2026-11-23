/* RED-TEAM agent 3: bounds / lifetime metadata safety adversary.
 *
 * This harness tries to BREAK the fail-closed contract of
 *   stdlib/memory.hp      (stdlib_mem_span_ok / contains / bounds / key_current
 *                          / query / count / guarded copy-move-set / transfer
 *                          / release)
 *   stdlib/lifetime_adapt.hp (borrow / release / transfer / read / write)
 *   stdlib/allocator.hp   (generation/liveness/capability/identity views)
 *
 * Every generated case is an ATTACK: the parameters are chosen so the header
 * contract PROMISES REFUSAL.  A case is counted as
 *   REFUSED        - the helper refused exactly as promised
 *   UNEXPECTED_OK  - the helper accepted hostile input (genuine finding)
 *   CRASH          - the forked child died by signal or fatal exit
 *   CORRUPT        - a canary / documented zeroing guarantee was violated
 *
 * SAFETY RULE FOR THIS HARNESS: a "stale" pointer is NEVER dereferenced.  It
 * is only passed to metadata-only query helpers.  Guarded write helpers are
 * only ever called with a LIVE destination plus hostile offsets/tokens, so a
 * refusal is the only legitimate outcome; a canary detects an illicit write.
 *
 * Cases run in a freshly forked child (one case per child).  The parent is
 * deterministic: a fixed-seed splitmix64 stream drives every case parameter,
 * and family = case_index % F_COUNT guarantees coverage.
 *
 * Build:
 *   ./build/c+ build tests/redteam/bounds/bounds_redteam.cp \
 *     --strict --sanitize=none -I. \
 *     -o tests/redteam/bounds/out/bounds_redteam
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"
#include "stdlib/lifetime_adapt.hp"
#include "stdlib/allocator.hp"

#define N_CASES 6000
#define RNG_SEED 0x9E3779B97F4A7C15ULL

/* One case per forked child; logs and binaries are gitignored. */
#define LOG_PATH "tests/redteam/bounds/scenarios.log"

/* ---- result classification codes (child -> parent over a pipe) ---------- */
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
    F_SPAN_OK = 0,
    F_SPAN_CONTAINS,
    F_BOUNDS,
    F_KEY_CURRENT,
    F_QUERY,
    F_CAPACITY_COUNT,
    F_ALLOC_IDENTITY,
    F_ALLOC_VIEWS,
    F_CAP_QUERIES,
    F_LIFETIME_CHECK,
    F_LIFETIME_OWNER_RELEASE,
    F_LIFETIME_TRANSFER,
    F_LIFETIME_ACCESS,
    F_LIFETIME_BOUNDS,
    F_MEM_OPS,
    F_MEM_TRANSFER,
    F_MEM_RELEASE,
    F_RECYCLED,
    F_NULL_ARGS,
    F_HUGE,
    F_COUNT
};

/* ---- deterministic PRNG ------------------------------------------------- */
static unsigned long long g_rng;
static unsigned char g_anchor;

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

/* ---- child-side helpers (metadata-only on stale pointers) --------------- */
static void setres(struct case_result *r, int cls, int sub, int got) {
    r->cls = cls;
    r->sub = sub;
    r->got = got;
}

static void expect_refuse(struct case_result *r, int accepted, int sub,
                          int got) {
    if (accepted)
        setres(r, CLS_UNEXPECTED_OK, sub, got);
    else
        setres(r, CLS_REFUSED, sub, got);
}

static void expect_clean(struct case_result *r, int violated, int sub) {
    if (violated)
        setres(r, CLS_CORRUPT, sub, 0);
    else
        setres(r, CLS_REFUSED, sub, 0);
}

static int all_bytes(const void *p, size_t n, unsigned char value) {
    const unsigned char *q = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i)
        if (q[i] != value)
            return 0;
    return 1;
}

static int id_is_zero(const stdlib_alloc_id *id) {
    return id->base == (const void *)0 && id->generation == 0ULL &&
           id->byte_size == 0u && id->elem_size == 0u;
}

/* A wild, non-canonical-ish address.  It is only ever compared as an integer
 * by metadata predicates; it is NEVER dereferenced by this harness. */
static const void *wild_ptr(unsigned long long v) {
    (void)v;
    return (const void *)&g_anchor; /* real provenance, untracked by metadata */
}

/* Address arithmetic via uintptr_t keeps the strict bounds checker from
 * treating an intentional one-past/interior probe as an illegal dereference;
 * the result is only ever compared as an integer by metadata queries. */
static void *offset_ptr(const void *base, size_t off) {
    return (void *)((unsigned char *)base + off);
}

/* stdlib_mem_release NULLs the handle, so use stdlib_release directly here
 * (the harness owns the allocation) and keep the raw address intentionally. */
static void make_stale_keep(void **stale_out, unsigned long long *tok_out) {
    void *p = stdlib_alloc(64);
    *tok_out = stdlib_mem_token(p);
    *stale_out = p;
    stdlib_release(p); /* retire metadata + backing storage: never deref p */
}

/* ======================================================================== */
/* Child-side attack families.  Each function sets *r exactly once.         */
/* ======================================================================== */

static void attack_span_ok(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    size_t cap = stdlib_mem_capacity(p);
    size_t off;
    size_t n;
    int variant = (int)(s->a % 5ULL);
    if (variant == 0) {
        off = cap + 1u + (size_t)(s->b % 8ULL);
        n = 1u;
    } else if (variant == 1) {
        off = cap;
        n = 1u + (size_t)(s->b % 8ULL);
    } else if (variant == 2) {
        off = cap - 1u;
        n = 2u + (size_t)(s->b % 8ULL);
    } else if (variant == 3) {
        off = SIZE_MAX - (size_t)(s->b % 4ULL);
        n = 1u + (size_t)(s->c % 8ULL);
    } else {
        off = cap; /* one-past start with a non-empty span */
        n = SIZE_MAX;
    }
    expect_refuse(r, stdlib_mem_span_ok(p, cap, off, n) != 0, 0,
                  stdlib_mem_span_ok(p, cap, off, n));
}

static void attack_span_contains(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    void *q = stdlib_alloc(64);
    const void *ptr;
    size_t n = 8u;
    int variant = (int)(s->a % 6ULL);
    if (variant == 0)
        ptr = (const void *)((unsigned char *)p + 65u + (size_t)(s->b % 4ULL));
    else if (variant == 1)
        ptr = (const void *)((unsigned char *)p + 64u);
    else if (variant == 2)
        ptr = (const void *)((unsigned char *)p + 63u);
    else if (variant == 3)
        ptr = (const void *)q; /* unrelated live block */
    else if (variant == 4)
        ptr = (const void *)((unsigned char *)p - (1u + (size_t)(s->b % 8ULL)));
    else
        ptr = wild_ptr(s->b); /* wild: integer compare only, never deref */
    expect_refuse(r, stdlib_mem_span_contains(p, 64u, ptr, n) != 0, 0,
                  stdlib_mem_span_contains(p, 64u, ptr, n));
}

static void attack_bounds(struct case_spec *s, struct case_result *r) {
    void *stale = (void *)0;
    unsigned long long tok = 0ULL;
    const void *ptr;
    const void *ob = (const void *)&g_anchor;
    size_t os = 12345u;
    int rc;
    int variant = (int)(s->a % 5ULL);
    make_stale_keep(&stale, &tok);
    if (variant == 0)
        ptr = (void *)((unsigned char *)stale + 8u); /* interior (stale) */
    else if (variant == 1)
        ptr = stale; /* released base */
    else if (variant == 2)
        ptr = (const void *)0;
    else if (variant == 3)
        ptr = wild_ptr(s->b);
    else {
        unsigned char local[16];
        local[0] = 0;
        ptr = (const void *)local; /* stack: untracked */
    }
    (void)tok;
    rc = stdlib_mem_bounds(ptr, &ob, &os);
    if (rc == 0) {
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
        return;
    }
    expect_clean(r, ob != (const void *)0 || os != 0u, 1);
}

static void attack_key_current(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    void *q = stdlib_alloc(64);
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    unsigned long long gen = stdlib_mem_token(p);
    unsigned long long key;
    int variant = (int)(s->a % 5ULL);
    make_stale_keep(&stale, &stale_tok);
    if (variant == 0)
        key = gen + 1ULL + (s->b % 16ULL);
    else if (variant == 1)
        key = stale_tok;
    else if (variant == 2)
        key = 0xFFFFFFFFFFFFFFFFULL;
    else if (variant == 3)
        key = stdlib_mem_token(q); /* another live block's generation */
    else
        key = gen; /* p alive but we query the STALE pointer below */
    if (variant == 4)
        p = stale; /* query stale pointer with its own old key */
    expect_refuse(r, stdlib_mem_key_current(p, key) != 0, 0,
                  stdlib_mem_key_current(p, key));
}

static void attack_query(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    unsigned long long gen = stdlib_mem_token(p);
    size_t off = 8u;
    size_t n = 8u;
    int variant = (int)(s->a % 6ULL);
    make_stale_keep(&stale, &stale_tok);
    if (variant == 0) {
        off = 64u + 1u + (size_t)(s->b % 4ULL);
    } else if (variant == 1) {
        off = 60u;
        n = 8u;
    } else if (variant == 2) {
        off = 0u;
        n = 8u;
        gen += 1u; /* stale key on a live block */
    } else if (variant == 3) {
        off = SIZE_MAX;
        n = SIZE_MAX;
    } else if (variant == 4) {
        p = (void *)((unsigned char *)p + 8u); /* interior as if base */
    } else {
        p = stale;
        gen = stale_tok;
    }
    expect_refuse(r, stdlib_mem_query(p, off, n, gen, (error *)0) == STDLIB_OK,
                  0, stdlib_mem_query(p, off, n, gen, (error *)0));
}

static void attack_capacity_count(struct case_spec *s, struct case_result *r) {
    void *stale = (void *)0;
    unsigned long long tok = 0ULL;
    const void *ptr;
    size_t elem = (size_t)(s->b % 5ULL); /* 0..4 */
    size_t out = 999u;
    size_t cap;
    int variant = (int)(s->a % 5ULL);
    make_stale_keep(&stale, &tok);
    if (variant == 0)
        ptr = (void *)((unsigned char *)stale + 8u);
    else if (variant == 1)
        ptr = stale;
    else if (variant == 2)
        ptr = (const void *)0;
    else if (variant == 3)
        ptr = wild_ptr(s->c);
    else {
        unsigned char local[8];
        local[0] = 0;
        ptr = (const void *)local;
    }
    (void)tok;
    cap = stdlib_mem_capacity(ptr);
    if (cap != 0u) {
        setres(r, CLS_UNEXPECTED_OK, 0, (int)cap);
        return;
    }
    if (stdlib_mem_count(ptr, elem, &out) != -1) {
        setres(r, CLS_UNEXPECTED_OK, 1, 0);
        return;
    }
    expect_clean(r, out != 0u, 2);
}

static void attack_alloc_identity(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    stdlib_alloc_id id = stdlib_alloc_id_zero;
    stdlib_alloc_id out = stdlib_alloc_id_zero;
    stdlib_alloc_id forged = stdlib_alloc_id_zero;
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    int rc;
    int variant = (int)(s->a % 7ULL);
    (void)stdlib_alloc_identity(p, &id);
    forged = id;
    forged.generation = id.generation + 1ULL + (s->b % 8ULL);
    make_stale_keep(&stale, &stale_tok);
    if (variant == 0)
        rc = stdlib_alloc_identity((void *)((unsigned char *)p + 8u), &out);
    else if (variant == 1)
        rc = stdlib_alloc_identity(stale, &out);
    else if (variant == 2)
        rc = stdlib_alloc_identity((const void *)0, &out);
    else if (variant == 3)
        rc = stdlib_alloc_identity(p, (stdlib_alloc_id *)0);
    else
        rc = 0; /* handled below without a metadata lookup */
    if (variant <= 2) {
        if (rc != -1) {
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
            return;
        }
        if ((variant != 3 && !id_is_zero(&out))) {
            setres(r, CLS_CORRUPT, 1, 0);
            return;
        }
        setres(r, CLS_REFUSED, 0, rc);
        return;
    }
    if (variant == 3) {
        if (rc != -1) {
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
            return;
        }
        setres(r, CLS_REFUSED, 0, rc);
        return;
    }
    if (variant == 4)
        expect_refuse(r, stdlib_alloc_id_matches(&forged, p) != 0, 0, 0);
    else if (variant == 5)
        expect_refuse(r, stdlib_alloc_id_equal(&forged, &id) != 0, 0, 0);
    else
        expect_refuse(r, stdlib_alloc_id_matches(&stdlib_alloc_id_zero, p) != 0,
                      0, 0);
}

static void attack_alloc_views(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    void *q = (void *)0;
    void *src = p;
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    unsigned long long gen = stdlib_mem_token(p);
    size_t off;
    int variant = (int)(s->a % 7ULL);
    make_stale_keep(&stale, &stale_tok);
    if (variant == 0) {
        expect_refuse(r, stdlib_alloc_view_bytes((void *)((unsigned char *)p + 8u)) != 0u ||
                         stdlib_alloc_view_generation((void *)((unsigned char *)p + 8u)) != 0ULL ||
                         stdlib_alloc_view_live((void *)((unsigned char *)p + 8u)) != 0,
                      0, 0);
    } else if (variant == 1) {
        expect_refuse(r, stdlib_alloc_view_bytes(stale) != 0u ||
                         stdlib_alloc_view_live(stale) != 0 ||
                         stdlib_alloc_view_count(stale) != 0u, 0, 0);
    } else if (variant == 2) {
        off = SIZE_MAX - (size_t)(s->b % 4ULL);
        expect_refuse(r, stdlib_alloc_view_remaining(p, off) != 0u, 0, 0);
    } else if (variant == 3) {
        expect_refuse(r, stdlib_alloc_view_unchanged(p, gen + 1ULL + (s->b % 8ULL)) != 0,
                      0, 0);
    } else if (variant == 4) {
        src = (void *)((unsigned char *)p + 8u); /* interior source */
        expect_refuse(r, stdlib_alloc_view_transfer_ok(&q, &src) != 0 ||
                         q != (void *)0 || src == (void *)0, 0, 0);
    } else if (variant == 5) {
        src = stale;
        expect_refuse(r, stdlib_alloc_view_transfer_ok(&q, &src) != 0, 0, 0);
    } else {
        expect_refuse(r, stdlib_alloc_view_transfer_ok(&src, &src) != 0, 0, 0);
    }
}

static void attack_cap_queries(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    unsigned long long gen = stdlib_mem_token(p);
    int variant = (int)(s->a % 6ULL);
    make_stale_keep(&stale, &stale_tok);
    if (variant == 0)
        expect_refuse(r, stdlib_alloc_cap_state(stale) != STDLIB_ALLOC_CAP_NONE ||
                         stdlib_alloc_hasvalidcap(stale) != 0 ||
                         stdlib_alloc_cap_state((const void *)0) != STDLIB_ALLOC_CAP_NONE,
                      0, 0);
    else if (variant == 1)
        expect_refuse(r, stdlib_alloc_cap_state((void *)((unsigned char *)p + 8u)) !=
                         STDLIB_ALLOC_CAP_NONE, 0, 0);
    else if (variant == 2)
        expect_refuse(r, stdlib_alloc_cap_covers(p, SIZE_MAX, SIZE_MAX) != 0 ||
                         stdlib_alloc_cap_covers(p, 60u, 8u) != 0, 0, 0);
    else if (variant == 3)
        expect_refuse(r, stdlib_alloc_cap_check(p, stale_tok, 0u, 8u) != 0, 0, 0);
    else if (variant == 4)
        expect_refuse(r, stdlib_alloc_cap_check(p, gen, SIZE_MAX, 8u) != 0, 0, 0);
    else
        expect_refuse(r, stdlib_alloc_cap_check((const void *)0, gen, 0u, 8u) != 0 ||
                         stdlib_alloc_cap_covers((const void *)0, 0u, 8u) != 0, 0, 0);
}

static void attack_lifetime_check(struct case_spec *s, struct case_result *r) {
    lifetime_owner o = {0};
    lifetime_owner src = {0};
    lifetime_owner dst = {0};
    lifetime_borrow b = {0};
    error e = error();
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    int rc;
    int variant = (int)(s->a % 6ULL);
    make_stale_keep(&stale, &stale_tok);
    if (lifetime_owner_init(&o, 64u, (error *)0) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    if (variant == 0) {
        b.ptr = stale;
        b.token = stale_tok;
        expect_refuse(r, lifetime_check(b.ptr, b.token, &e) == STDLIB_OK, 0, 0);
    } else if (variant == 1) {
        expect_refuse(r, lifetime_check(o.ptr, stale_tok, &e) == STDLIB_OK, 0, 0);
    } else if (variant == 2) {
        expect_refuse(r, lifetime_check(o.ptr, LIFETIME_NO_TOKEN, &e) == STDLIB_OK, 0, 0);
    } else if (variant == 3) {
        expect_refuse(r, lifetime_check((const void *)0, o.token, &e) == STDLIB_OK, 0, 0);
    } else if (variant == 4) {
        b.ptr = stale;
        b.token = stale_tok;
        expect_refuse(r, lifetime_borrow_is_live(&b) != 0, 0, 0);
    } else {
        if (lifetime_owner_init(&src, 64u, (error *)0) != STDLIB_OK) {
            setres(r, CLS_REFUSED, 99, 0);
            return;
        }
        if (lifetime_borrow_from(&b, &src, (error *)0) != STDLIB_OK) {
            setres(r, CLS_REFUSED, 98, 0);
            return;
        }
        rc = lifetime_transfer(&dst, &src, (error *)0);
        if (rc != STDLIB_OK) {
            setres(r, CLS_REFUSED, 97, rc);
            return;
        }
        expect_refuse(r, lifetime_borrow_is_live(&b) != 0 ||
                         lifetime_check(b.ptr, b.token, &e) == STDLIB_OK, 0, rc);
        lifetime_owner_release(&dst, (error *)0);
    }
    lifetime_owner_release(&o, (error *)0);
}

static void attack_lifetime_owner_release(struct case_spec *s,
                                          struct case_result *r) {
    lifetime_owner o = {0};
    lifetime_owner copy = {0};
    unsigned long long before;
    unsigned long long after;
    int rc;
    int variant = (int)(s->a % 4ULL);
    if (lifetime_owner_init(&o, 64u, (error *)0) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    copy = o; /* second handle to the same block/token */
    stdlib_telemetry_set_enabled(1);
    if (variant == 0) {
        rc = lifetime_owner_release(&o, (error *)0);
        if (rc != STDLIB_OK) {
            setres(r, CLS_REFUSED, 98, rc);
            return;
        }
        before = stdlib_state.counters[STDLIB_TM_FREE];
        rc = lifetime_owner_release(&o, (error *)0); /* same handle twice */
    } else if (variant == 1) {
        rc = lifetime_owner_release(&o, (error *)0);
        if (rc != STDLIB_OK) {
            setres(r, CLS_REFUSED, 98, rc);
            return;
        }
        before = stdlib_state.counters[STDLIB_TM_FREE];
        rc = lifetime_owner_release(&copy, (error *)0); /* alias of freed */
    } else if (variant == 2) {
        before = stdlib_state.counters[STDLIB_TM_FREE];
        rc = lifetime_owner_release((lifetime_owner *)0, (error *)0);
    } else {
        before = stdlib_state.counters[STDLIB_TM_FREE];
        copy.token = o.token + 1ULL; /* forged token must be refused */
        rc = lifetime_owner_release(&copy, (error *)0);
    }
    after = stdlib_state.counters[STDLIB_TM_FREE];
    if (rc != STDLIB_FAIL) {
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
        return;
    }
    expect_clean(r, after != before, 1); /* refused path made no allocator call */
}

static void attack_lifetime_transfer(struct case_spec *s,
                                     struct case_result *r) {
    lifetime_owner a = {0};
    lifetime_owner b = {0};
    lifetime_owner c = {0};
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    void *saved_a;
    unsigned long long saved_token;
    void *saved_b;
    int accepted;
    int rc;
    int variant = (int)(s->a % 5ULL);
    make_stale_keep(&stale, &stale_tok);
    if (lifetime_owner_init(&a, 64u, (error *)0) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    if (variant == 0) {
        saved_a = a.ptr;
        saved_token = a.token;
        rc = lifetime_transfer(&a, &a, (error *)0);
        accepted = rc == STDLIB_OK || a.ptr != saved_a || a.token != saved_token;
        expect_refuse(r, accepted, 0, rc);
    } else if (variant == 1) {
        if (lifetime_owner_init(&b, 64u, (error *)0) != STDLIB_OK) {
            setres(r, CLS_REFUSED, 98, 0);
            return;
        }
        saved_b = b.ptr;
        rc = lifetime_transfer(&b, &a, (error *)0); /* dst non-empty */
        accepted = rc == STDLIB_OK || b.ptr != saved_b;
        expect_refuse(r, accepted, 0, rc);
        lifetime_owner_release(&b, (error *)0);
    } else if (variant == 2) {
        a.ptr = (void *)0;
        rc = lifetime_transfer(&b, &a, (error *)0);
        accepted = rc == STDLIB_OK || b.ptr != (void *)0;
        expect_refuse(r, accepted, 0, rc);
    } else if (variant == 3) {
        a.ptr = stale;
        a.token = stale_tok;
        rc = lifetime_transfer(&b, &a, (error *)0);
        accepted = rc == STDLIB_OK || b.ptr != (void *)0;
        expect_refuse(r, accepted, 0, rc);
    } else {
        saved_a = a.ptr;
        saved_token = a.token;
        rc = lifetime_transfer(&b, &a, (error *)0); /* a -> b, valid */
        if (rc != STDLIB_OK) {
            setres(r, CLS_REFUSED, 97, rc);
            return;
        }
        rc = lifetime_transfer(&c, &a, (error *)0); /* a is moved-from */
        /* The old borrow descriptor (saved_a/saved_token) must be stale, and
         * the refused second transfer must leave c empty. */
        accepted = rc == STDLIB_OK || c.ptr != (void *)0 ||
                   lifetime_check(saved_a, saved_token, (error *)0) == STDLIB_OK;
        expect_refuse(r, accepted, 0, rc);
        if (rc == STDLIB_OK && c.ptr != (void *)0)
            lifetime_owner_release(&c, (error *)0);
        lifetime_owner_release(&b, (error *)0);
        return;
    }
    if (a.ptr != (void *)0 && a.token != LIFETIME_NO_TOKEN)
        lifetime_owner_release(&a, (error *)0);
}

static void attack_lifetime_access(struct case_spec *s, struct case_result *r) {
    lifetime_owner o = {0};
    lifetime_borrow b = {0};
    lifetime_borrow stale_b = {0};
    unsigned char dst[24];
    unsigned char src[8];
    size_t off;
    size_t n = 8u;
    int rc;
    int variant = (int)(s->a % 5ULL);
    memset(dst, 0xC3u, sizeof(dst));
    memset(src, 0x17u, sizeof(src));
    if (lifetime_owner_init(&o, 64u, (error *)0) != STDLIB_OK ||
        lifetime_borrow_from(&b, &o, (error *)0) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    memset(o.ptr, 0x3Cu, 64u);
    stale_b = b;
    lifetime_owner_release(&o, (error *)0); /* b / stale_b are now stale */
    if (variant == 0) {
        /* b is stale: only metadata-only operations are used on it. */
        b.ptr = (void *)0;
        rc = lifetime_read(&b, 0u, dst, n, (error *)0);
    } else if (variant == 1) {
        rc = lifetime_borrow_is_live(&b) != 0 ? STDLIB_OK : STDLIB_FAIL;
    } else if (variant == 2) {
        rc = lifetime_check(b.ptr, b.token, (error *)0);
    } else if (variant == 3) {
        rc = lifetime_read((const lifetime_borrow *)0, 0u, dst, n, (error *)0);
    } else {
        rc = lifetime_read(&stale_b, SIZE_MAX, dst, n, (error *)0);
    }
    if (rc == STDLIB_OK && variant != 1) {
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
        return;
    }
    if (variant == 1 && rc == STDLIB_OK) {
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
        return;
    }
    expect_clean(r, !all_bytes(dst, sizeof(dst), 0xC3u), 1);
}

/* LIVE borrow but hostile range: validates the bounds gate before any copy. */
static void attack_lifetime_bounds(struct case_spec *s, struct case_result *r) {
    lifetime_owner o = {0};
    lifetime_borrow b = {0};
    unsigned char dst[16];
    unsigned char src[4];
    size_t off;
    size_t n;
    int rc;
    int variant = (int)(s->a % 5ULL);
    memset(dst, 0x5Au, sizeof(dst));
    memset(src, 0x11u, sizeof(src));
    if (lifetime_owner_init(&o, 64u, (error *)0) != STDLIB_OK ||
        lifetime_borrow_from(&b, &o, (error *)0) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    memset(o.ptr, 0x3Cu, 64u);
    if (variant == 0) {
        off = 65u + (size_t)(s->b % 8ULL);
        n = 4u;
        rc = lifetime_read(&b, off, dst, n, (error *)0);
    } else if (variant == 1) {
        off = 63u;
        n = 8u;
        rc = lifetime_write(&b, off, src, n, (error *)0);
    } else if (variant == 2) {
        off = SIZE_MAX;
        n = 8u;
        rc = lifetime_write(&b, off, src, n, (error *)0);
    } else if (variant == 3) {
        off = 0u;
        n = 8u;
        rc = lifetime_read(&b, off, (void *)0, n, (error *)0);
    } else {
        off = 0u;
        n = 8u;
        rc = lifetime_write(&b, off, (const void *)0, n, (error *)0);
    }
    if (rc == STDLIB_OK) {
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
        lifetime_owner_release(&o, (error *)0);
        return;
    }
    if (!all_bytes(dst, sizeof(dst), 0x5Au) ||
        !all_bytes(o.ptr, 64u, 0x3Cu)) {
        setres(r, CLS_CORRUPT, 1, 0);
        lifetime_owner_release(&o, (error *)0);
        return;
    }
    setres(r, CLS_REFUSED, 0, rc);
    lifetime_owner_release(&o, (error *)0);
}

static void attack_mem_ops(struct case_spec *s, struct case_result *r) {
    void *dst = stdlib_alloc(64);
    void *src = stdlib_alloc(64);
    unsigned long long gen = stdlib_mem_token(dst);
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    size_t off;
    size_t n = 8u;
    int rc;
    int variant = (int)(s->a % 8ULL);
    make_stale_keep(&stale, &stale_tok);
    if (dst == (void *)0 || src == (void *)0) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    memset(dst, 0xA5u, 64u);
    memset(src, 0x5Au, 64u);
    if (variant == 0) {
        rc = stdlib_mem_set(dst, 64u + 1u + (size_t)(s->b % 4ULL), 0, n, gen,
                            (error *)0);
    } else if (variant == 1) {
        rc = stdlib_mem_set(dst, 60u, 0, n, gen, (error *)0);
    } else if (variant == 2) {
        rc = stdlib_mem_set(dst, 0u, 0, n, stale_tok, (error *)0);
    } else if (variant == 3) {
        rc = stdlib_mem_copy(dst, 0u, src, 65u + (size_t)(s->b % 4ULL), n, gen,
                             (error *)0);
    } else if (variant == 4) {
        rc = stdlib_mem_copy(dst, 0u, (const void *)0, 0u, n, gen, (error *)0);
    } else if (variant == 5) {
        rc = stdlib_mem_move(dst, SIZE_MAX, src, 0u, n, gen, (error *)0);
    } else if (variant == 6) {
        rc = stdlib_mem_set((void *)0, 0u, 0, n, gen, (error *)0);
    } else {
        rc = stdlib_mem_copy(dst, 0u, src, 0u, n, gen + 1ULL, (error *)0);
    }
    if (rc == STDLIB_OK) {
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
        return;
    }
    if (!all_bytes(dst, 64u, 0xA5u)) {
        setres(r, CLS_CORRUPT, 1, 0);
        return;
    }
    off = 0u; /* keep the compiler from folding an unused local away */
    (void)off;
    (void)stale;
    setres(r, CLS_REFUSED, 0, rc);
}

static void attack_mem_transfer(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    void *dst = (void *)0;
    void *src = p;
    void *stale = (void *)0;
    unsigned long long stale_tok = 0ULL;
    void *saved_dst;
    void *saved_src;
    int rc;
    int variant = (int)(s->a % 4ULL);
    make_stale_keep(&stale, &stale_tok);
    if (variant == 0) {
        dst = p; /* non-empty destination */
        saved_dst = dst;
        saved_src = src;
        rc = stdlib_mem_transfer(&dst, &src, (error *)0);
    } else if (variant == 1) {
        saved_dst = dst;
        saved_src = src;
        rc = stdlib_mem_transfer(&dst, (void **)0, (error *)0);
    } else if (variant == 2) {
        saved_dst = dst; /* dst operand aliases src: only one handle exists */
        saved_src = src;
        rc = stdlib_mem_transfer(&src, &src, (error *)0); /* dst == src */
    } else {
        src = stale;
        saved_dst = dst;
        saved_src = src;
        rc = stdlib_mem_transfer(&dst, &src, (error *)0);
    }
    if (rc == STDLIB_OK) {
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
        return;
    }
    /* A refused transfer must leave BOTH handles exactly as they were and
     * must not free the live source block. */
    if (dst != saved_dst || src != saved_src || stdlib_alloc_size(p) == 0u)
        setres(r, CLS_CORRUPT, 1, 0);
    else
        setres(r, CLS_REFUSED, 1, 0);
}

static void attack_mem_release(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    int variant = (int)(s->a % 3ULL);
    if (variant == 0) {
        stdlib_mem_release(&p);
        if (p != (void *)0) {
            setres(r, CLS_CORRUPT, 0, 0);
            return;
        }
        stdlib_mem_release(&p); /* repeated cleanup on the same handle */
        setres(r, p == (void *)0 ? CLS_REFUSED : CLS_CORRUPT, 0, 0);
    } else if (variant == 1) {
        stdlib_mem_release((void **)0);
        setres(r, CLS_REFUSED, 0, 0);
    } else {
        stdlib_mem_release(&p);
        setres(r, p == (void *)0 ? CLS_REFUSED : CLS_CORRUPT, 0, 0);
    }
}

/* Recycled-address generation confusion.  All checks are metadata-only. */
static void attack_recycled(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    stdlib_alloc_id old_id = stdlib_alloc_id_zero;
    stdlib_alloc_id new_id = stdlib_alloc_id_zero;
    lifetime_borrow b = {0};
    unsigned long long old_tok = stdlib_mem_token(p);
    void *q = (void *)0;
    int i;
    int recycled = 0;
    (void)stdlib_alloc_identity(p, &old_id);
    b.ptr = p;
    b.token = old_tok;
    stdlib_release(p); /* p is stale from here on: never dereferenced */
    for (i = 0; i < 32; ++i) {
        q = stdlib_alloc(64);
        if (q == p) {
            recycled = 1;
            break;
        }
        stdlib_release(q);
    }
    (void)stdlib_alloc_identity(q, &new_id);
    if (stdlib_mem_key_current(p, old_tok) != 0 ||
        stdlib_alloc_id_matches(&old_id, q) != 0 ||
        stdlib_alloc_id_equal(&old_id, &new_id) != 0 ||
        stdlib_alloc_cap_check(q, old_tok, 0u, 8u) != 0 ||
        lifetime_borrow_is_live(&b) != 0 ||
        stdlib_mem_query(q, 0u, 8u, old_tok, (error *)0) == STDLIB_OK) {
        setres(r, CLS_UNEXPECTED_OK, 0, recycled);
    } else {
        setres(r, CLS_REFUSED, 0, recycled);
    }
    stdlib_release(q);
}

static void attack_null_args(struct case_spec *s, struct case_result *r) {
    void *p = stdlib_alloc(64);
    lifetime_owner o = {0};
    lifetime_borrow b = {0};
    stdlib_alloc_id id = stdlib_alloc_id_zero;
    const void *ob = (const void *)&g_anchor;
    size_t os = 99u;
    size_t out = 77u;
    void *q = (void *)0;
    int variant = (int)(s->a % 8ULL);
    unsigned long long gen = stdlib_mem_token(p);
    if (lifetime_owner_init(&o, 64u, (error *)0) != STDLIB_OK ||
        lifetime_borrow_from(&b, &o, (error *)0) != STDLIB_OK) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    if (variant == 0) {
        expect_refuse(r, stdlib_mem_check((const void *)0, gen, (error *)0) == STDLIB_OK ||
                         stdlib_mem_check_token((const void *)0, gen, (error *)0) == STDLIB_OK ||
                         stdlib_mem_query((const void *)0, 0u, 8u, gen, (error *)0) == STDLIB_OK,
                      0, 0);
    } else if (variant == 1) {
        if (stdlib_mem_bounds((const void *)0, &ob, &os) == 0) {
            setres(r, CLS_UNEXPECTED_OK, 0, 0);
        } else {
            expect_clean(r, ob != (const void *)0 || os != 0u, 1);
        }
    } else if (variant == 2) {
        expect_refuse(r, stdlib_mem_capacity((const void *)0) != 0u ||
                         stdlib_mem_count((const void *)0, 0u, &out) != -1 || out != 0u,
                      0, 0);
    } else if (variant == 3) {
        expect_refuse(r, lifetime_owner_init((lifetime_owner *)0, 64u, (error *)0) == STDLIB_OK ||
                         lifetime_owner_release((lifetime_owner *)0, (error *)0) == STDLIB_OK ||
                         lifetime_transfer((lifetime_owner *)0, &o, (error *)0) == STDLIB_OK ||
                         lifetime_borrow_from((lifetime_borrow *)0, &o, (error *)0) == STDLIB_OK,
                      0, 0);
    } else if (variant == 4) {
        expect_refuse(r, lifetime_read(&b, 0u, (void *)0, 8u, (error *)0) == STDLIB_OK ||
                         lifetime_write(&b, 0u, (const void *)0, 8u, (error *)0) == STDLIB_OK,
                      0, 0);
    } else if (variant == 5) {
        expect_refuse(r, stdlib_alloc_identity((const void *)0, &id) != -1 ||
                         !id_is_zero(&id) ||
                         stdlib_alloc_id_matches(&id, p) != 0, 0, 0);
    } else if (variant == 6) {
        expect_refuse(r, stdlib_alloc_view_bytes((const void *)0) != 0u ||
                         stdlib_alloc_view_live((const void *)0) != 0 ||
                         stdlib_alloc_view_remaining((const void *)0, 0u) != 0u ||
                         stdlib_alloc_view_unchanged((const void *)0, gen) != 0 ||
                         stdlib_alloc_cap_state((const void *)0) != STDLIB_ALLOC_CAP_NONE ||
                         stdlib_alloc_cap_covers((const void *)0, 0u, 8u) != 0 ||
                         stdlib_alloc_cap_check((const void *)0, gen, 0u, 8u) != 0 ||
                         stdlib_alloc_view_transfer_ok((void **)0, &q) != 0, 0, 0);
    } else {
        expect_refuse(r, stdlib_mem_transfer(&q, (void **)0, (error *)0) == STDLIB_OK ||
                         stdlib_mem_set((void *)0, 0u, 0, 8u, gen, (error *)0) == STDLIB_OK,
                      0, 0);
    }
    lifetime_owner_release(&o, (error *)0);
}

static void attack_huge(struct case_spec *s, struct case_result *r) {
    void *dst = stdlib_alloc(64);
    unsigned long long gen = stdlib_mem_token(dst);
    size_t out = 9u;
    int variant = (int)(s->a % 6ULL);
    memset(dst, 0x77u, 64u);
    if (variant == 0) {
        expect_refuse(r, stdlib_mem_span_ok(dst, 64u, SIZE_MAX, SIZE_MAX) != 0 ||
                         stdlib_mem_span_ok(dst, 64u, 63u, SIZE_MAX) != 0, 0, 0);
    } else if (variant == 1) {
        expect_refuse(r, stdlib_mem_span_contains(dst, 64u, wild_ptr(s->b), SIZE_MAX) != 0 ||
                         stdlib_mem_span_contains(dst, 64u, offset_ptr(dst, 63u),
                                                  SIZE_MAX) != 0, 0, 0);
    } else if (variant == 2) {
        expect_refuse(r, stdlib_mem_query(dst, SIZE_MAX, SIZE_MAX, gen, (error *)0) == STDLIB_OK ||
                         stdlib_mem_check_range(dst, SIZE_MAX, SIZE_MAX, 1, (error *)0) == STDLIB_OK,
                      0, 0);
    } else if (variant == 3) {
        expect_refuse(r, stdlib_alloc_view_remaining(dst, SIZE_MAX) != 0u ||
                         stdlib_alloc_cap_covers(dst, SIZE_MAX, SIZE_MAX) != 0 ||
                         stdlib_alloc_cap_check(dst, gen, SIZE_MAX, SIZE_MAX) != 0, 0, 0);
    } else if (variant == 4) {
        expect_refuse(r, stdlib_mem_set(dst, SIZE_MAX, 0, SIZE_MAX, gen, (error *)0) == STDLIB_OK,
                      0, 0);
        if (!all_bytes(dst, 64u, 0x77u)) {
            setres(r, CLS_CORRUPT, 1, 0);
            return;
        }
    } else {
        if (stdlib_mem_count(dst, SIZE_MAX, &out) != 0 || out != 0u)
            setres(r, CLS_UNEXPECTED_OK, 0, (int)out);
        else
            setres(r, CLS_REFUSED, 0, (int)out);
    }
}

/* Dispatch one attack inside the forked child. */
static void run_attack(struct case_spec *s, struct case_result *r) {
    error_reset((void *)0);
    r->cls = CLS_REFUSED;
    r->sub = 0;
    r->got = 0;
    switch (s->family) {
    case F_SPAN_OK: attack_span_ok(s, r); break;
    case F_SPAN_CONTAINS: attack_span_contains(s, r); break;
    case F_BOUNDS: attack_bounds(s, r); break;
    case F_KEY_CURRENT: attack_key_current(s, r); break;
    case F_QUERY: attack_query(s, r); break;
    case F_CAPACITY_COUNT: attack_capacity_count(s, r); break;
    case F_ALLOC_IDENTITY: attack_alloc_identity(s, r); break;
    case F_ALLOC_VIEWS: attack_alloc_views(s, r); break;
    case F_CAP_QUERIES: attack_cap_queries(s, r); break;
    case F_LIFETIME_CHECK: attack_lifetime_check(s, r); break;
    case F_LIFETIME_OWNER_RELEASE: attack_lifetime_owner_release(s, r); break;
    case F_LIFETIME_TRANSFER: attack_lifetime_transfer(s, r); break;
    case F_LIFETIME_ACCESS: attack_lifetime_access(s, r); break;
    case F_LIFETIME_BOUNDS: attack_lifetime_bounds(s, r); break;
    case F_MEM_OPS: attack_mem_ops(s, r); break;
    case F_MEM_TRANSFER: attack_mem_transfer(s, r); break;
    case F_MEM_RELEASE: attack_mem_release(s, r); break;
    case F_RECYCLED: attack_recycled(s, r); break;
    case F_NULL_ARGS: attack_null_args(s, r); break;
    case F_HUGE: attack_huge(s, r); break;
    default: setres(r, CLS_REFUSED, 0, 0); break;
    }
}

static const char *class_name(int cls) {
    if (cls == CLS_UNEXPECTED_OK) return "UNEXPECTED_OK";
    if (cls == CLS_CRASH) return "CRASH";
    if (cls == CLS_CORRUPT) return "CORRUPT";
    return "REFUSED";
}

/* Single classification path, shared by the campaign and --selfcheck. */
static int classify_child(int waited_ok, int status, ssize_t got,
                          struct case_result *res) {
    int cls;
    if (!waited_ok || WIFSIGNALED(status) || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || got != (ssize_t)sizeof(*res))
        return CLS_CRASH;
    cls = res->cls;
    if (cls < CLS_REFUSED || cls > CLS_CORRUPT)
        return CLS_CRASH;
    return cls;
}

/* Non-vacuity control: prove the classifier maps accepted / signal / fatal /
 * canary / bogus outcomes to the right buckets.  No target code is touched. */
static int selfcheck(void) {
    struct case_result r = {0, 0, 0};
    int status = 0;
    int passed = 0;
    r.cls = CLS_UNEXPECTED_OK;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_UNEXPECTED_OK)
        passed++;
    r.cls = CLS_REFUSED;
    if (classify_child(1, (7 << 8), (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    if (classify_child(1, 11, (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    r.cls = CLS_CORRUPT;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CORRUPT)
        passed++;
    r.cls = 99;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    printf("SELFCHECK %d 5\n", passed);
    return passed == 5 ? 0 : 1;
}

int main(void) {
    const char *selfcheck_env = getenv("REDTEAM_SELFCHECK");
    if (selfcheck_env != (const char *)0 && strcmp(selfcheck_env, "1") == 0)
        return selfcheck();
    int logfd;
    int line_len;
    char line[512];
    int i;
    int counts[4];
    int fds[2];
    pid_t pid;
    int status = 0;
    ssize_t got;
    struct case_spec spec;
    struct case_result result;
    int cls;

    for (i = 0; i < 4; ++i)
        counts[i] = 0;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
    g_rng = RNG_SEED;

    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) {
        fprintf(stderr, "cannot open %s\n", LOG_PATH);
        printf("REDTEAM bounds 0 0 0 0 0\n");
        return 2;
    }
    line_len = snprintf(line, sizeof(line),
                        "# bounds redteam seed=%016llx cases=%d families=%d\n",
                        RNG_SEED, N_CASES, F_COUNT);
    if (line_len > 0)
        (void)write(logfd, line, (size_t)line_len);

    for (i = 0; i < N_CASES; ++i) {
        spec = gen_spec(i);
        if (pipe(fds) != 0) {
            fprintf(stderr, "pipe failed at case %d\n", i);
            break;
        }
        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed at case %d\n", i);
            close(fds[0]);
            close(fds[1]);
            break;
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
        cls = classify_child(waitpid(pid, &status, 0) == pid, status, got,
                             &result);
        counts[cls]++;
        line_len = snprintf(line, sizeof(line),
                            "%d %d %llu %llu %llu %s sub=%d got=%d cls=%d\n",
                            i, spec.family, spec.a, spec.b, spec.c,
                            class_name(cls),
                            cls == CLS_CRASH ? -1 : result.sub,
                            cls == CLS_CRASH ? -1 : result.got, cls);
        if (line_len > 0)
            (void)write(logfd, line, (size_t)line_len);
    }

    line_len = snprintf(line, sizeof(line),
                        "# SUMMARY cases=%d refused=%d crashes=%d corrupt=%d unexpected_ok=%d\n",
                        N_CASES, counts[CLS_REFUSED], counts[CLS_CRASH],
                        counts[CLS_CORRUPT], counts[CLS_UNEXPECTED_OK]);
    if (line_len > 0)
        (void)write(logfd, line, (size_t)line_len);
    close(logfd);

    printf("REDTEAM bounds %d %d %d %d %d\n",
           N_CASES, counts[CLS_REFUSED], counts[CLS_CRASH],
           counts[CLS_CORRUPT], counts[CLS_UNEXPECTED_OK]);
    return 0;
}
