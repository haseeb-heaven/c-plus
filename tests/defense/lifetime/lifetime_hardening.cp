/* BLUE-TEAM agent 10: regression tests for the additive hardening of
 * stdlib/memory.hp and stdlib/lifetime_adapt.hp.
 *
 * Each check prints "<tag> <0|1>" and the final line prints
 * "LIFETIME_HARDEN <checks> <failures>"; exit status is 0 only when no check
 * failed.  All stale pointers are metadata-only inputs: nothing here
 * dereferences a freed block.  Errors are inspected through the error object,
 * never through a raw pointer to freed memory.
 *
 * Build (from the repository root):
 *   ./build/c+ build tests/defense/lifetime/lifetime_hardening.cp \
 *     --strict --sanitize=none -I. -o <out>
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"
#include "stdlib/lifetime_adapt.hp"

static int h_checks = 0;
static int h_failures = 0;

static void h_check(const char *tag, int cond) {
    h_checks++;
    if (!cond)
        h_failures++;
    print(tag, cond ? 1 : 0);
}

static int h_err_is(error *e, int failed, int category, int code) {
    if (!failed)
        return e->failed_now() == 0 &&
               e->category() == STDLIB_CATEGORY_OK &&
               e->code() == (int)CPLUS_EXIT_OK;
    return e->failed_now() == 1 && e->category() == category &&
           e->code() == code && e->what()[0] != '\0';
}

/* GAP 1: key == 0 used to fail OPEN for NULL/untracked/stale pointers. */
static void gap_key_zero(void) {
    error e = error();
    unsigned char stackbuf[8];
    void *p = stdlib_alloc(32);
    void *stale;
    unsigned long long tok;

    h_check("key_null_zero_refused", stdlib_mem_key_current((const void *)0, 0ULL) == 0);
    h_check("key_untracked_zero_refused",
            stdlib_mem_key_current((const void *)stackbuf, 0ULL) == 0);

    h_check("key_alloc", p != (void *)0);
    if (p == (void *)0)
        return;
    tok = stdlib_mem_token(p);
    h_check("key_valid_token_current", stdlib_mem_key_current(p, tok) == 1);
    h_check("key_valid_zero_still_current", stdlib_mem_key_current(p, 0ULL) == 1);
    h_check("key_valid_wrong_token_stale", stdlib_mem_key_current(p, tok + 1ULL) == 0);

    /* Stale alias held after release with key == 0: fail-open before the fix. */
    stale = p;
    stdlib_mem_release(&p);
    h_check("key_freed_zero_refused", stdlib_mem_key_current(stale, 0ULL) == 0);
    h_check("key_freed_token_refused", stdlib_mem_key_current(stale, tok) == 0);
    h_check("key_query_clean", h_err_is(&e, 0, 0, 0));
}

/* GAP 2: token-less release on a stale alias could call cplus_release() twice
 * (address recycle / double-free).  The additive checked helper must refuse. */
static void gap_checked_release(void) {
    error e = error();
    void *p = stdlib_alloc(32);
    void *stale;
    unsigned long long tok;
    unsigned char guard[8];

    h_check("rel_alloc", p != (void *)0);
    if (p == (void *)0)
        return;
    memset(guard, 0xA5, sizeof(guard));
    tok = stdlib_mem_token(p);
    stale = p;

    /* NULL handle is an idempotent no-op, matching stdlib_mem_release(). */
    h_check("rel_null_noop", stdlib_mem_release_checked((void **)0, 0ULL, &e) == STDLIB_OK);
    h_check("rel_null_clean", h_err_is(&e, 0, 0, 0));

    /* Wrong token on a live block is refused without freeing it. */
    e.clear();
    h_check("rel_wrong_token_refused",
            stdlib_mem_release_checked(&p, tok + 1ULL, &e) == STDLIB_FAIL &&
                p != (void *)0);
    h_check("rel_wrong_token_error",
            h_err_is(&e, 1, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE));

    /* Correct token releases and NULLs the handle. */
    e.clear();
    h_check("rel_valid", stdlib_mem_release_checked(&p, tok, &e) == STDLIB_OK &&
                             p == (void *)0);
    h_check("rel_valid_clean", h_err_is(&e, 0, 0, 0));
    h_check("rel_repeated_noop", stdlib_mem_release_checked(&p, tok, &e) == STDLIB_OK);

    /* The alias is now stale: untracked or (if recycled) generation-mismatched.
     * Either way it must be refused with LIFECYCLE and must not be freed. */
    e.clear();
    h_check("rel_stale_alias_refused",
            stdlib_mem_release_checked(&stale, tok, &e) == STDLIB_FAIL);
    h_check("rel_stale_alias_error",
            h_err_is(&e, 1, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE));
    h_check("rel_guard_intact", guard[0] == 0xA5 && guard[7] == 0xA5);
}

/* GAP 3: owners had no side-effect-free liveness query. */
static void gap_owner_live(void) {
    error e = error();
    lifetime_owner o = lifetime_owner();
    lifetime_owner src = lifetime_owner();
    lifetime_owner dst = lifetime_owner();

    h_check("owner_null_not_live", lifetime_owner_is_live((const lifetime_owner *)0) == 0);
    h_check("owner_empty_not_live", lifetime_owner_is_live(&o) == 0);

    h_check("owner_init", lifetime_owner_init(&o, 64u, &e) == STDLIB_OK);
    h_check("owner_live", lifetime_owner_is_live(&o) == 1);

    h_check("owner_src_init", lifetime_owner_init(&src, 64u, &e) == STDLIB_OK);
    h_check("owner_transfer", lifetime_transfer(&dst, &src, &e) == STDLIB_OK);
    h_check("owner_dst_live_after_transfer", lifetime_owner_is_live(&dst) == 1);
    h_check("owner_src_not_live_after_transfer", lifetime_owner_is_live(&src) == 0);

    h_check("owner_release", lifetime_owner_release(&o, &e) == STDLIB_OK);
    h_check("owner_not_live_after_release", lifetime_owner_is_live(&o) == 0);
    h_check("owner_double_release_refused",
            lifetime_owner_release(&o, &e) == STDLIB_FAIL &&
                h_err_is(&e, 1, STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_DOUBLE_FREE));

    h_check("owner_release_dst", lifetime_owner_release(&dst, &e) == STDLIB_OK);
}

/* Preserve documented bounds/lifetime semantics through the hardened query. */
static void preserved_semantics(void) {
    error e = error();
    void *p = stdlib_alloc(16);
    unsigned long long tok;
    const void *base = (const void *)0;
    size_t size = 0;

    h_check("sem_alloc", p != (void *)0);
    if (p == (void *)0)
        return;
    tok = stdlib_mem_token(p);
    stdlib_mem_bounds(p, &base, &size);

    h_check("sem_span_exact", stdlib_mem_span_ok(base, size, 0u, 16u) == 1);
    h_check("sem_span_overflow_rejected", stdlib_mem_span_ok(base, size, SIZE_MAX, 2u) == 0);
    h_check("sem_contains_interior", stdlib_mem_span_contains(base, size, (const unsigned char *)p + 4u, 4u) == 1);
    h_check("sem_query_ok", stdlib_mem_query(p, 0u, 16u, tok, &e) == STDLIB_OK);
    e.clear();
    h_check("sem_query_bounds", stdlib_mem_query(p, 13u, 4u, tok, &e) == STDLIB_FAIL &&
                                   h_err_is(&e, 1, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS));
    e.clear();
    h_check("sem_query_stale", stdlib_mem_query(p, 0u, 16u, tok + 1ULL, &e) == STDLIB_FAIL &&
                                  h_err_is(&e, 1, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE));

    stdlib_mem_release(&p);
}

int main(void) {
    gap_key_zero();
    gap_checked_release();
    gap_owner_live();
    preserved_semantics();

    print("LIFETIME_HARDEN", h_checks, h_failures);
    return h_failures == 0 ? 0 : 1;
}
