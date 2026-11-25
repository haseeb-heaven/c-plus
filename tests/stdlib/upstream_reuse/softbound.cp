/* Regression test for the SoftBoundCETS technique ported into stdlib/memory.hp.
 *
 * Upstream anchor (fetched 2026-11-23, master
 * 9a9c09f04e16f2d1ef3a906fd138a7b89df44996, softboundcets-lib/softboundcets.h):
 *   L539/L555  if ((ptr < base) || ((char*)ptr + size) > bound) abort();
 *   L576/L620  if (dest < dest_base || dest > dest_bound - size) abort();
 *   L1004      if (*lock != key) abort();       // temporal CETS check
 *   L1078      *lock = 0;                       // invalidate on deallocation
 *
 * C+ ports only the CHECK PREDICATES: stdlib_mem_span_ok / span_contains /
 * bounds / key_current / query in stdlib/memory.hp.  No pointer is widened,
 * no ABI symbol is added, and no out-of-bounds memory is ever touched here.
 *
 * Cases: exact-fit, one-over, interior pointer (with and without explicit
 * base), stale generation (transfer and release/realloc), plus the pure
 * metadata bounds query.  Prints "<tag> <0|1>" per check and
 * "SOFTBOUND <checks> <failures>"; exit 0 only when failures == 0.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static int sb_checks = 0;
static int sb_failures = 0;

static void sb_check(const char *tag, int cond) {
    sb_checks++;
    if (!cond)
        sb_failures++;
    print(tag, cond ? 1 : 0);
}

static int sb_err_is(error *e, int failed, int category, int code) {
    int ok;
    if (!failed) {
        ok = e->failed_now() == 0 &&
             e->category() == STDLIB_CATEGORY_OK &&
             e->code() == (int)CPLUS_EXIT_OK;
    } else {
        ok = e->failed_now() == 1 && e->category() == category &&
             e->code() == code && e->what()[0] != '\0';
    }
    return ok;
}

/* Strict mode rejects a raw `p[i]` loop on a pointer parameter (extent cannot
 * be proven), so compare through memcmp against a stack pattern instead. */
static int sb_all_byte(const unsigned char *p, size_t n, unsigned char v) {
    unsigned char expect[16];
    if (n > sizeof(expect))
        return 0;
    memset(expect, v, n);
    return memcmp(p, expect, n) == 0;
}

/* ---- metadata bounds query --------------------------------------------- */

static void sb_bounds_query(void) {
    error e = error();
    const void *base = (const void *)0;
    size_t size = 999;
    unsigned char stackbuf[8];
    void *p = stdlib_alloc(32);

    sb_check("bounds_alloc", p != (void *)0);
    if (p == (void *)0)
        return;

    sb_check("bounds_tracked_rc",
             stdlib_mem_bounds(p, &base, &size) == 0);
    sb_check("bounds_tracked_base", base == p);
    sb_check("bounds_tracked_size", size == 32);

    base = (const void *)0;
    size = 999;
    sb_check("bounds_untracked_rc",
             stdlib_mem_bounds((const void *)stackbuf, &base, &size) == -1);
    sb_check("bounds_untracked_zeroed",
             base == (const void *)0 && size == 0);

    base = (const void *)0;
    size = 999;
    sb_check("bounds_null_rc",
             stdlib_mem_bounds((const void *)0, &base, &size) == -1);
    sb_check("bounds_null_zeroed",
             base == (const void *)0 && size == 0);

    sb_check("bounds_null_outputs",
             stdlib_mem_bounds(p, (const void **)0, (size_t *)0) == -1);
    sb_check("bounds_query_clean", sb_err_is(&e, 0, 0, 0));

    stdlib_mem_release(&p);
}

/* ---- exact fit ---------------------------------------------------------- */

static void sb_exact_fit(void) {
    error e = error();
    void *p = stdlib_alloc(16);
    const void *base = (const void *)0;
    size_t size = 0;
    unsigned long long key;
    int ok;

    sb_check("exact_alloc", p != (void *)0);
    if (p == (void *)0)
        return;

    sb_check("exact_bounds", stdlib_mem_bounds(p, &base, &size) == 0 &&
                                size == 16);
    key = stdlib_mem_token(p);
    sb_check("exact_token", key != 0);

    sb_check("exact_span_full", stdlib_mem_span_ok(base, size, 0, 16) == 1);
    sb_check("exact_span_interior", stdlib_mem_span_ok(base, size, 4, 12) == 1);
    sb_check("exact_contains_full",
             stdlib_mem_span_contains(base, size, p, 16) == 1);

    sb_check("exact_query_full",
             stdlib_mem_query(p, 0, 16, key, &e) == STDLIB_OK &&
                 sb_err_is(&e, 0, 0, 0));
    sb_check("exact_query_interior",
             stdlib_mem_query(p, 4, 12, key, &e) == STDLIB_OK &&
                 sb_err_is(&e, 0, 0, 0));
    sb_check("exact_query_zero_len",
             stdlib_mem_query(p, 16, 0, key, &e) == STDLIB_OK);

    /* Repeated queries must not consume or change the key/lock. */
    ok = 1;
    for (size = 0; size < 3; size++)
        ok = ok && stdlib_mem_query(p, 0, 16, key, &e) == STDLIB_OK &&
             stdlib_mem_key_current(p, key) == 1;
    sb_check("exact_key_stable", ok);

    stdlib_mem_release(&p);
}

/* ---- one over ----------------------------------------------------------- */

static void sb_one_over(void) {
    error e = error();
    unsigned char *p = (unsigned char *)stdlib_alloc(16);
    void *h = (void *)p;
    const void *base = (const void *)0;
    size_t size = 0;
    unsigned long long key;
    size_t i;

    sb_check("over_alloc", h != (void *)0);
    if (h == (void *)0)
        return;
    for (i = 0; i < 16; i++)
        p[i] = 0xEE;
    stdlib_mem_bounds(h, &base, &size);
    key = stdlib_mem_token(h);

    sb_check("over_span_1", stdlib_mem_span_ok(base, size, 0, 17) == 0);
    sb_check("over_span_tail", stdlib_mem_span_ok(base, size, 13, 4) == 0);
    sb_check("over_span_off", stdlib_mem_span_ok(base, size, 17, 0) == 0);
    sb_check("over_contains_1",
             stdlib_mem_span_contains(base, size, p + 1, 16) == 0);

    e.clear();
    sb_check("over_query_full",
             stdlib_mem_query(h, 0, 17, key, &e) == STDLIB_FAIL &&
                 sb_err_is(&e, 1, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS));
    sb_check("over_query_sentinel_full", sb_all_byte(p, 16, 0xEE));

    e.clear();
    sb_check("over_query_one_past",
             stdlib_mem_query(h, 16, 1, key, &e) == STDLIB_FAIL &&
                 sb_err_is(&e, 1, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS));
    sb_check("over_query_sentinel_tail", sb_all_byte(p, 16, 0xEE));

    /* Empty span (size 0) fails closed, as does a NULL base. */
    sb_check("over_empty_span", stdlib_mem_span_ok(base, 0, 0, 0) == 0);
    sb_check("over_null_span", stdlib_mem_span_ok((const void *)0, 16, 0, 1) == 0);

    stdlib_mem_release(&h);
}

/* ---- interior pointer --------------------------------------------------- */

static void sb_interior_pointer(void) {
    error e = error();
    unsigned char *p = (unsigned char *)stdlib_alloc(32);
    void *h = (void *)p;
    const void *base = (const void *)0;
    size_t size = 0;
    unsigned long long key;

    sb_check("interior_alloc", h != (void *)0);
    if (h == (void *)0)
        return;
    stdlib_mem_bounds(h, &base, &size);
    key = stdlib_mem_token(h);

    /* An interior pointer has no metadata of its own: callers must supply
     * (base, offset) rather than a pre-summed pointer alone. */
    sb_check("interior_no_metadata",
             stdlib_mem_capacity((const void *)(p + 8)) == 0);

    sb_check("interior_contains_exact",
             stdlib_mem_span_contains(base, size, p + 8, 24) == 1);
    sb_check("interior_contains_one_over",
             stdlib_mem_span_contains(base, size, p + 8, 25) == 0);
    sb_check("interior_one_past_zero",
             stdlib_mem_span_contains(base, size, p + 32, 0) == 1);
    sb_check("interior_one_past_one",
             stdlib_mem_span_contains(base, size, p + 32, 1) == 0);

    /* A pointer into a different block BELOW this one: addresses are only
     * compared as integers, never read.  Strict mode forbids manufacturing a
     * pointer from an integer, so use a second real allocation. */
    {
        void *other_h = stdlib_alloc(8);
        const void *other = other_h;
        sb_check("interior_other_alloc", other_h != (void *)0);
        if (other_h != (void *)0) {
            if ((uintptr_t)other < (uintptr_t)base)
                sb_check("interior_below_base",
                         stdlib_mem_span_contains(base, size, other, 1) == 0);
            else
                sb_check("interior_below_base",
                         stdlib_mem_span_contains(other, 8, base, 1) == 0);
            stdlib_mem_release(&other_h);
        }
    }

    sb_check("interior_query",
             stdlib_mem_query(h, 8, 24, key, &e) == STDLIB_OK &&
                 sb_err_is(&e, 0, 0, 0));

    stdlib_mem_release(&h);
}

/* ---- stale generation (CETS key/lock) ---------------------------------- */

static void sb_stale_generation(void) {
    error e = error();
    unsigned char *p = (unsigned char *)stdlib_alloc(16);
    void *h = (void *)p;
    void *q = (void *)0;
    unsigned long long key;
    size_t i;

    sb_check("stale_alloc", h != (void *)0);
    if (h == (void *)0)
        return;
    for (i = 0; i < 16; i++)
        p[i] = 0x5A;
    key = stdlib_mem_token(h);
    sb_check("stale_key_current", stdlib_mem_key_current(h, key) == 1);
    sb_check("stale_key_zero_opts_out",
             stdlib_mem_key_current(h, 0ULL) == 1);

    /* Transfer bumps the metadata generation: the captured key is now stale. */
    sb_check("stale_transfer", stdlib_mem_transfer(&q, &h, &e) == STDLIB_OK &&
                                   h == (void *)0 && q != (void *)0);
    sb_check("stale_key_after_transfer", stdlib_mem_key_current(q, key) == 0);
    e.clear();
    sb_check("stale_query_after_transfer",
             stdlib_mem_query(q, 0, 16, key, &e) == STDLIB_FAIL &&
                 sb_err_is(&e, 1, STDLIB_CATEGORY_LIFECYCLE,
                           CPLUS_EXIT_USE_AFTER_FREE));
    /* p still aliases the transferred block and has provable extent. */
    sb_check("stale_query_no_write", sb_all_byte(p, 16, 0x5A));
    sb_check("stale_fresh_key_ok",
             stdlib_mem_query(q, 0, 16, stdlib_mem_token(q), &e) == STDLIB_OK);
    stdlib_mem_release(&q);

    /* Release + reallocate also changes the generation of the live block. */
    {
        void *r = stdlib_alloc(8);
        void *s;
        unsigned long long rkey;
        sb_check("stale_realloc_alloc", r != (void *)0);
        if (r != (void *)0) {
            rkey = stdlib_mem_token(r);
            stdlib_mem_release(&r);
            s = stdlib_alloc(8);
            sb_check("stale_realloc_alloc2", s != (void *)0);
            if (s != (void *)0) {
                sb_check("stale_key_after_realloc",
                         stdlib_mem_key_current(s, rkey) == 0);
                stdlib_mem_release(&s);
            }
        }
    }
}

int main(void) {
    sb_bounds_query();
    sb_exact_fit();
    sb_one_over();
    sb_interior_pointer();
    sb_stale_generation();

    print("SOFTBOUND", sb_checks, sb_failures);
    return sb_failures == 0 ? 0 : 1;
}
