/* adv1000_alloc.c -- 1000 deterministic adversarial scenarios against the
 * checked allocation/free APIs of libcplus_safecrt.a (+ libcplus_hardened.a).
 *
 * SCOPE (verified present with nm; macOS symbols carry a leading underscore):
 *   malloc_s, calloc_s, realloc_s, free_s, free_checked_s, aligned_alloc_s,
 *   cplus_safe_ptr_is_live / cplus_safe_ptr_size / cplus_safe_ptr_was_freed,
 *   cplus_safe_alloc_fail_after / cplus_safe_alloc_fail_reset.
 *
 * Every scenario performs an operation that is either expected to succeed or
 * expected to be REFUSED, then checks, independently:
 *   (1) the refusal code / NULL (with the mandated errno) -- `rc`;
 *   (2) canary/content integrity after the refusal -- `canary_ok`;
 *   (3) metadata state (live / was_freed / tracked size) -- `state_ok`.
 * Failed-realloc scenarios additionally assert atomicity: the ORIGINAL
 * pointer is still live, its tracked size is unchanged, and its bytes still
 * hold the canary (i.e. the refused operation touched neither metadata nor
 * payload).
 *
 * Determinism: a fixed 64-bit LCG (Numerical Recipes constants, no time, no
 * randomness, no stdin) is reseeded per scenario with BASE_SEED and the id.
 * All numeric parameters (sizes 0..65536, counts 0..1024, alignments 1..256,
 * injection points, variants) come from that PRNG. Family assignment is a
 * deterministic round-robin so all families are covered evenly; ids 1..4 are
 * pinned to the four required breaking cases.
 *
 * This is EVIDENCE, not a proof of all memory safety. See NOTES.md.
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "safe_alloc.h"
#include "safe_error.h"

#define N_SCENARIOS   1000u
#define MAX_SIZE      65536u   /* inclusive */
#define MAX_COUNT     1024u    /* inclusive */
#define BASE_SEED     0xC0FFEEAD1000ULL
#define RSIZE         ((size_t)(SIZE_MAX >> 1))
#define ZERO_WINDOW   4096u    /* bounded zero-verification window */
#define ZERO_TAIL     64u

typedef struct { uint64_t state; } rng;

static void rng_init(rng *r, uint64_t seed) {
    r->state = seed ? seed : 0x9E3779B97F4A7C15ULL;
}
static uint32_t rnd(rng *r) {
    r->state = r->state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(r->state >> 32);
}
static size_t rnd_size(rng *r) { return (size_t)(rnd(r) % (MAX_SIZE + 1u)); }
static size_t rnd_count(rng *r) { return (size_t)(rnd(r) % (MAX_COUNT + 1u)); }
static unsigned rnd_align_any(rng *r) { return 1u + (rnd(r) % 256u); }
static size_t rnd_align_valid(rng *r) {
    static const size_t a[] = { 8u, 16u, 32u, 64u, 128u, 256u };
    return a[rnd(r) % (sizeof(a) / sizeof(a[0]))];
}

static unsigned char canary_value(unsigned id) {
    return (unsigned char)(0x5Au ^ (unsigned char)((id * 131u) & 0xFFu));
}
static void canary_fill(void *p, size_t n, unsigned char v) {
    if (p) memset(p, (int)v, n);
}
static int canary_ok(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    if (!b) return 0;
    for (i = 0; i < n; ++i) {
        if (b[i] != v) return 0;
    }
    return 1;
}
static int zero_ok(const unsigned char *p, size_t n) {
    size_t i, head = n < ZERO_WINDOW ? n : ZERO_WINDOW;
    for (i = 0; i < head; ++i) if (p[i] != 0) return 0;
    if (n > ZERO_WINDOW) {
        size_t tail = n < ZERO_TAIL ? n : ZERO_TAIL;
        for (i = n - tail; i < n; ++i) if (p[i] != 0) return 0;
    }
    return 1;
}
static size_t req_size(size_t n) { return n == 0 ? 1u : n; }

/* ---- small live-block tracker (keeps every scenario leak-free) ---------- */
typedef struct { void *p[16]; int n; } live_set;
static void live_add(live_set *ls, void *p) {
    if (p && ls->n < (int)(sizeof(ls->p) / sizeof(ls->p[0]))) ls->p[ls->n++] = p;
}
static void live_replace(live_set *ls, void *old, void *now) {
    int i;
    for (i = 0; i < ls->n; ++i) {
        if (ls->p[i] == old) { ls->p[i] = now; return; }
    }
    live_add(ls, now);
}
static void live_cleanup(live_set *ls) {
    int i;
    for (i = 0; i < ls->n; ++i) {
        if (ls->p[i] && cplus_safe_ptr_is_live(ls->p[i])) {
            (void)free_checked_s(ls->p[i]);
        }
    }
    ls->n = 0;
}

/* ---- per-scenario result ------------------------------------------------ */
typedef struct {
    const char *api;
    const char *cas;
    int rc;                /* observed return code (errno for pointer APIs) */
    int expected_rc;       /* code the contract mandates                  */
    int refusal_expected;  /* 1 when the operation must be refused        */
    int canary_ok;         /* canary/content integrity held               */
    int state_ok;          /* metadata (live/freed/size) matched          */
} result;

enum {
    F_MALLOC_OK, F_MALLOC_OVERFLOW, F_CALLOC_OK, F_CALLOC_OVERFLOW,
    F_REALLOC_OK, F_REALLOC_FAIL, F_REALLOC_OVERFLOW, F_REALLOC_INVALID,
    F_FREE_OK, F_DOUBLE_FREE, F_INVALID_FREE, F_ALIGNED_OK,
    F_ALIGNED_INVALID, F_INJECT_ALLOC, N_FAMILIES
};

static unsigned family_for(unsigned id, rng *r) {
    (void)r;
    /* The four required BREAKING cases are pinned to fixed ids. */
    if (id == 1u) return F_MALLOC_OVERFLOW;   /* n = SIZE_MAX */
    if (id == 2u) return F_CALLOC_OVERFLOW;   /* count*size overflow */
    if (id == 3u) return F_REALLOC_FAIL;      /* failed realloc, original live */
    if (id == 4u) return F_DOUBLE_FREE;       /* refused double free */
    return (unsigned)((id - 1u) % N_FAMILIES);
}

/* ------------------------------- families ------------------------------- */

static void sc_malloc_ok(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t n = rnd_size(r);
    void *p;
    (void)id;
    res->api = "malloc_s"; res->cas = "alloc";
    res->expected_rc = 0; res->refusal_expected = 0;
    errno = 0;
    p = malloc_s(n);
    res->rc = p ? 0 : errno;
    if (!p) { res->state_ok = 0; return; }
    live_add(ls, p);
    canary_fill(p, req_size(n), v);
    if (!canary_ok(p, req_size(n), v)) res->canary_ok = 0;
    if (!cplus_safe_ptr_is_live(p) || cplus_safe_ptr_size(p) != req_size(n)) res->state_ok = 0;
}

static void sc_malloc_overflow(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t bad = (id == 1u) ? SIZE_MAX : ((rnd(r) & 1u) ? SIZE_MAX : (RSIZE + 1u));
    void *p;
    (void)ls; (void)v;
    res->api = "malloc_s";
    res->cas = (bad == SIZE_MAX) ? "overflow_size_max" : "overflow_rsize";
    res->expected_rc = EINVAL; res->refusal_expected = 1;
    errno = 0;
    p = malloc_s(bad);
    res->rc = p ? 0 : errno;
    if (p) { res->state_ok = 0; (void)free_checked_s(p); }
}

static void sc_calloc_ok(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t count = rnd_count(r);
    size_t es = rnd_size(r);
    /* Contract: if EITHER argument is zero, calloc_s uses (1,1) and returns a
     * defined 1-byte zeroed block with elem_size 1 (not (1,es)). */
    size_t ec = (count == 0u || es == 0u) ? 1u : count;
    size_t eff = (count == 0u || es == 0u) ? 1u : es;
    size_t total = ec * eff;
    cplus_safe_alloc_meta m;
    unsigned char *p;
    (void)id; (void)v;
    res->api = "calloc_s"; res->cas = "alloc_zeroed";
    res->expected_rc = 0; res->refusal_expected = 0;
    errno = 0;
    p = (unsigned char *)calloc_s(count, es);
    res->rc = p ? 0 : errno;
    if (!p) { res->state_ok = 0; return; }
    live_add(ls, p);
    if (!zero_ok(p, total)) res->canary_ok = 0;   /* content integrity = zeroed */
    if (!cplus_safe_ptr_is_live(p) || cplus_safe_ptr_size(p) != total) res->state_ok = 0;
    if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK ||
        m.elem_size != eff || !m.is_live) res->state_ok = 0;
}

static void sc_calloc_overflow(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t count, es;
    void *p;
    unsigned pick = (id == 2u) ? 0u : (unsigned)(rnd(r) % 4u);
    (void)ls; (void)v;
    switch (pick) {
        case 0: count = SIZE_MAX;        es = 2u;        break;
        case 1: count = 2u;              es = SIZE_MAX;  break;
        case 2: count = RSIZE + 1u;      es = 2u;        break;
        default: count = SIZE_MAX / 2u + 1u; es = 2u;    break;
    }
    res->api = "calloc_s"; res->cas = "count_size_overflow";
    res->expected_rc = EOVERFLOW; res->refusal_expected = 1;
    errno = 0;
    p = calloc_s(count, es);
    res->rc = p ? 0 : errno;
    if (p) { res->state_ok = 0; (void)free_checked_s(p); }
}

static void sc_realloc_ok(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t n = rnd_size(r);
    size_t nn = rnd_size(r);
    size_t old_req = req_size(n), new_req = req_size(nn);
    size_t keep = old_req < new_req ? old_req : new_req;
    void *subject = malloc_s(n);
    void *p2;
    (void)id;
    res->api = "realloc_s"; res->cas = (nn > n) ? "grow" : "shrink";
    res->expected_rc = 0; res->refusal_expected = 0;
    if (!subject) { res->state_ok = 0; return; }
    live_add(ls, subject);
    canary_fill(subject, old_req, v);
    errno = 0;
    p2 = realloc_s(subject, nn);
    res->rc = p2 ? 0 : errno;
    if (!p2) { res->state_ok = 0; return; }
    live_replace(ls, subject, p2);
    if (!canary_ok(p2, keep, v)) res->canary_ok = 0;   /* content preserved */
    if (!cplus_safe_ptr_is_live(p2) || cplus_safe_ptr_size(p2) != new_req) res->state_ok = 0;
}

static void sc_realloc_fail(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t n = 128u + (size_t)(rnd(r) % 4096u);
    size_t nn = n + 1u + (size_t)(rnd(r) % 4096u);
    void *subject = malloc_s(n);
    void *p2;
    int e;
    (void)id;
    res->api = "realloc_s"; res->cas = "inject_fail_atomic";
    res->expected_rc = ENOMEM; res->refusal_expected = 1;
    if (!subject) { res->state_ok = 0; return; }
    live_add(ls, subject);
    canary_fill(subject, n, v);
    cplus_safe_alloc_fail_after(0L);       /* next allocation MUST fail */
    errno = 0;
    p2 = realloc_s(subject, nn);
    e = errno;
    cplus_safe_alloc_fail_reset();
    res->rc = p2 ? 0 : e;
    if (p2) {
        live_replace(ls, subject, p2);
        res->state_ok = 0;
    } else {
        /* ATOMICITY: original pointer, size and payload all untouched. */
        if (!cplus_safe_ptr_is_live(subject) || cplus_safe_ptr_size(subject) != n) res->state_ok = 0;
        if (!canary_ok(subject, n, v)) res->canary_ok = 0;
    }
}

static void sc_realloc_overflow(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t n = 64u + (size_t)(rnd(r) % 4096u);
    size_t bad = (id == 1u) ? SIZE_MAX : SIZE_MAX; /* n = SIZE_MAX boundary */
    void *subject = malloc_s(n);
    void *p2;
    (void)bad;
    res->api = "realloc_s"; res->cas = "overflow_atomic";
    res->expected_rc = EINVAL; res->refusal_expected = 1;
    if (!subject) { res->state_ok = 0; return; }
    live_add(ls, subject);
    canary_fill(subject, n, v);
    errno = 0;
    p2 = realloc_s(subject, SIZE_MAX);
    res->rc = p2 ? 0 : errno;
    if (p2) {
        live_replace(ls, subject, p2);
        res->state_ok = 0;
    } else {
        if (!cplus_safe_ptr_is_live(subject) || cplus_safe_ptr_size(subject) != n) res->state_ok = 0;
        if (!canary_ok(subject, n, v)) res->canary_ok = 0;
    }
}

static void sc_realloc_invalid(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t n = 64u + (size_t)(rnd(r) % 4096u);
    void *subject = malloc_s(n);
    void *bad = NULL;
    void *raw = NULL;
    unsigned variant = (unsigned)(rnd(r) % 3u);
    void *p2;
    int stack_var = 0x1234;
    (void)id;
    res->api = "realloc_s";
    res->expected_rc = EINVAL; res->refusal_expected = 1;
    if (!subject) { res->state_ok = 0; return; }
    live_add(ls, subject);
    canary_fill(subject, n, v);
    if (variant == 0u) {
        bad = &stack_var; res->cas = "invalid_stack";
    } else if (variant == 1u) {
        bad = (void *)((unsigned char *)subject + 1); res->cas = "invalid_interior";
    } else {
        raw = malloc(n);
        if (!raw) { res->state_ok = 0; return; }
        bad = raw; res->cas = "invalid_foreign";
    }
    errno = 0;
    p2 = realloc_s(bad, n + 8u);
    res->rc = p2 ? 0 : errno;
    if (raw) free(raw);                 /* raw block was never in the table */
    if (p2) { res->state_ok = 0; (void)free_checked_s(p2); }
    if (!cplus_safe_ptr_is_live(subject) || cplus_safe_ptr_size(subject) != n) res->state_ok = 0;
    if (!canary_ok(subject, n, v)) res->canary_ok = 0;
}

static void sc_free_ok(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t n = 1u + (size_t)(rnd(r) % 4096u);
    void *subject = malloc_s(n);
    unsigned use_checked = (unsigned)(rnd(r) & 1u);
    (void)id; (void)v;
    res->api = use_checked ? "free_checked_s" : "free_s";
    res->cas = "valid_free";
    res->expected_rc = 0; res->refusal_expected = 0;
    if (!subject) { res->state_ok = 0; return; }
    live_add(ls, subject);
    canary_fill(subject, n, v);
    errno = 0;
    if (use_checked) {
        res->rc = free_checked_s(subject);
    } else {
        free_s(subject);
        res->rc = errno;               /* free_s delegates to free_checked_s */
    }
    if (cplus_safe_ptr_is_live(subject) || !cplus_safe_ptr_was_freed(subject)) res->state_ok = 0;
}

static void sc_double_free(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t n = 1u + (size_t)(rnd(r) % 4096u);
    void *subject = malloc_s(n);
    unsigned second_checked = (unsigned)(rnd(r) & 1u);
    (void)id; (void)v;
    res->api = second_checked ? "free_checked_s" : "free_s";
    res->cas = "double_free";
    res->expected_rc = EINVAL; res->refusal_expected = 1;
    if (!subject) { res->state_ok = 0; return; }
    live_add(ls, subject);
    canary_fill(subject, n, v);
    (void)free_checked_s(subject);     /* first release succeeds */
    errno = 0;
    if (second_checked) {
        res->rc = free_checked_s(subject);
    } else {
        free_s(subject);
        res->rc = errno;               /* refused -> EINVAL from free_checked_s */
    }
    if (cplus_safe_ptr_is_live(subject) || !cplus_safe_ptr_was_freed(subject)) res->state_ok = 0;
    /* canary_ok here is the always-live guard block checked by run_scenario(). */
}

static void sc_invalid_free(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t n = 64u + (size_t)(rnd(r) % 4096u);
    void *subject = malloc_s(n);
    void *bad = NULL;
    void *raw = NULL;
    unsigned variant = (unsigned)(rnd(r) % 4u);
    int stack_var = 0;
    static int static_var = 0;
    (void)id;
    res->api = "free_checked_s";
    res->expected_rc = EINVAL; res->refusal_expected = 1;
    if (!subject) { res->state_ok = 0; return; }
    live_add(ls, subject);
    canary_fill(subject, n, v);
    if (variant == 0u) {
        bad = &stack_var; res->cas = "invalid_free_stack";
    } else if (variant == 1u) {
        bad = &static_var; res->cas = "invalid_free_static";
    } else if (variant == 2u) {
        raw = malloc(n);
        if (!raw) { res->state_ok = 0; return; }
        bad = raw; res->cas = "invalid_free_foreign";
    } else {
        bad = (void *)((unsigned char *)subject + 1u); res->cas = "free_interior";
    }
    errno = 0;
    res->rc = free_checked_s(bad);
    if (raw) free(raw);                 /* refused: raw block still ours */
    if (!cplus_safe_ptr_is_live(subject) || cplus_safe_ptr_size(subject) != n) res->state_ok = 0;
    if (!canary_ok(subject, n, v)) res->canary_ok = 0;
}

static void sc_aligned_ok(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t a = rnd_align_valid(r);
    size_t n = rnd_size(r);
    void *p;
    (void)id;
    res->api = "aligned_alloc_s"; res->cas = "aligned_alloc";
    res->expected_rc = 0; res->refusal_expected = 0;
    errno = 0;
    p = aligned_alloc_s(a, n);
    res->rc = p ? 0 : errno;
    if (!p) { res->state_ok = 0; return; }
    live_add(ls, p);
    canary_fill(p, req_size(n), v);
    if (((uintptr_t)p & (a - 1u)) != 0u) res->state_ok = 0;
    if (!canary_ok(p, req_size(n), v)) res->canary_ok = 0;
    if (!cplus_safe_ptr_is_live(p) || cplus_safe_ptr_size(p) != req_size(n)) res->state_ok = 0;
}

static void sc_aligned_invalid(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    size_t a, n = rnd_size(r);
    unsigned variant = (unsigned)(rnd(r) % 3u);
    void *p;
    (void)id; (void)ls; (void)v;
    if (variant == 0u) {
        static const size_t bad[] = { 0u, 1u, 2u, 3u, 4u, 6u, 7u, 12u, 24u, 48u, 100u };
        a = bad[rnd(r) % (sizeof(bad) / sizeof(bad[0]))];
        res->cas = "bad_alignment";
    } else if (variant == 1u) {
        a = 16u; n = SIZE_MAX; res->cas = "bad_size_max";
    } else {
        a = rnd_align_any(r); n = RSIZE + 1u; res->cas = "bad_size_rsize";
    }
    res->api = "aligned_alloc_s";
    res->expected_rc = EINVAL; res->refusal_expected = 1;
    errno = 0;
    p = aligned_alloc_s(a, n);
    res->rc = p ? 0 : errno;
    if (p) { res->state_ok = 0; (void)free_checked_s(p); }
}

static void sc_inject_alloc(unsigned id, rng *r, live_set *ls, result *res, unsigned char v) {
    unsigned k = (unsigned)(rnd(r) % 8u);   /* successes before the failure */
    void *ok[8];
    void *badp;
    void *after;
    unsigned i;
    int e;
    (void)id;
    res->api = "alloc_fail_after"; res->cas = "inject_then_refuse";
    res->expected_rc = ENOMEM; res->refusal_expected = 1;
    /* fail_after(k): the next k allocation calls succeed, then every later
     * call is refused with ENOMEM until fail_reset(). The guard was allocated
     * earlier (before injection) so its canary is a valid witness. */
    cplus_safe_alloc_fail_after((long)k);
    for (i = 0; i < k; ++i) {
        errno = 0;
        ok[i] = malloc_s(32u + (size_t)rnd(r) % 512u);
        if (!ok[i]) { res->state_ok = 0; break; }
        live_add(ls, ok[i]);
        canary_fill(ok[i], 32u, v);
    }
    errno = 0;
    badp = malloc_s(64u);                   /* the (k+1)-th call: ENOMEM */
    e = errno;
    cplus_safe_alloc_fail_reset();          /* reset restores allocation */
    res->rc = badp ? 0 : e;
    if (badp) { res->state_ok = 0; live_add(ls, badp); }
    for (i = 0; i < k; ++i) {
        if (!ok[i]) break;
        if (!cplus_safe_ptr_is_live(ok[i]) || !canary_ok(ok[i], 32u, v)) res->state_ok = 0;
    }
    after = malloc_s(24u);
    if (!after) {
        res->state_ok = 0;
    } else {
        live_add(ls, after);
        canary_fill(after, 24u, v);
        if (!canary_ok(after, 24u, v)) res->canary_ok = 0;
    }
}

/* --------------------------------- driver -------------------------------- */
static result run_scenario(unsigned id) {
    rng r;
    result res;
    live_set ls;
    unsigned char v = canary_value(id);
    void *guard;
    unsigned fam;

    memset(&res, 0, sizeof res);
    memset(&ls, 0, sizeof ls);
    res.canary_ok = 1; res.state_ok = 1;

    rng_init(&r, BASE_SEED ^ ((uint64_t)id * 0x9E3779B97F4A7C15ULL));
    cplus_safe_alloc_fail_reset();       /* clean injection state per scenario */

    /* Always-present canary guard, allocated BEFORE any fail_after() call. */
    guard = malloc_s(64u);
    if (!guard) {
        res.api = "guard"; res.cas = "setup_failed";
        res.state_ok = 0; res.canary_ok = 0;
        return res;
    }
    live_add(&ls, guard);
    canary_fill(guard, 64u, v);

    fam = family_for(id, &r);
    switch (fam) {
        case F_MALLOC_OK:       sc_malloc_ok(id, &r, &ls, &res, v); break;
        case F_MALLOC_OVERFLOW: sc_malloc_overflow(id, &r, &ls, &res, v); break;
        case F_CALLOC_OK:       sc_calloc_ok(id, &r, &ls, &res, v); break;
        case F_CALLOC_OVERFLOW: sc_calloc_overflow(id, &r, &ls, &res, v); break;
        case F_REALLOC_OK:      sc_realloc_ok(id, &r, &ls, &res, v); break;
        case F_REALLOC_FAIL:    sc_realloc_fail(id, &r, &ls, &res, v); break;
        case F_REALLOC_OVERFLOW:sc_realloc_overflow(id, &r, &ls, &res, v); break;
        case F_REALLOC_INVALID: sc_realloc_invalid(id, &r, &ls, &res, v); break;
        case F_FREE_OK:         sc_free_ok(id, &r, &ls, &res, v); break;
        case F_DOUBLE_FREE:     sc_double_free(id, &r, &ls, &res, v); break;
        case F_INVALID_FREE:    sc_invalid_free(id, &r, &ls, &res, v); break;
        case F_ALIGNED_OK:      sc_aligned_ok(id, &r, &ls, &res, v); break;
        case F_ALIGNED_INVALID: sc_aligned_invalid(id, &r, &ls, &res, v); break;
        case F_INJECT_ALLOC:    sc_inject_alloc(id, &r, &ls, &res, v); break;
        default: res.api = "unknown"; res.cas = "unknown"; res.state_ok = 0; break;
    }

    /* The guard never participates in an operation under test: it must be
     * byte-for-byte intact, proving a refused operation caused no corruption. */
    if (!canary_ok(guard, 64u, v)) res.canary_ok = 0;

    live_cleanup(&ls);
    cplus_safe_alloc_fail_reset();
    return res;
}

int main(void) {
    unsigned id;
    unsigned long refused = 0, canary_total = 0, state_total = 0, failures = 0;

    for (id = 1u; id <= N_SCENARIOS; ++id) {
        result res = run_scenario(id);
        int this_fail = 0;

        if (res.rc != res.expected_rc) this_fail = 1;
        if (!res.canary_ok) this_fail = 1;
        if (!res.state_ok) this_fail = 1;

        if (res.refusal_expected && res.rc == res.expected_rc) ++refused;
        if (res.canary_ok) ++canary_total;
        if (res.state_ok) ++state_total;
        if (this_fail) ++failures;

        printf("S %u %s %s %d %d %d\n",
               id, res.api, res.cas, res.rc, res.canary_ok, res.state_ok);
    }

    printf("ADV1000 alloc %u %lu %lu %lu %lu\n",
           N_SCENARIOS, refused, canary_total, state_total, failures);
    return failures ? 1 : 0;
}
