/* Upstream-reuse regression test for stdlib/lifetime_adapt.hp.
 *
 * Adapts SaferCPlusPlus's token-based scope-pointer lifetime mechanism to the
 * C+ stdlib generation-token metadata (see lifetime_adapt_NOTES.md).
 *
 * Deterministic and self-checking: no stdin, no wall clock, no dereference of
 * a stale/released pointer.  Every check prints "<tag> <0|1>" and the final
 * line is "LIFETIME_ADAPT <checks> <failures>".  The process exits non-zero iff
 * failures != 0.
 *
 * Coverage: valid borrow, stale refusal via metadata (never dereference the
 * stale pointer), ownership transfer invalidation, and double-release refusal.
 */
#include <stddef.h>
#include <string.h>
#include "stdlib/lifetime_adapt.hp"

static int la_checks = 0;
static int la_failures = 0;

static void la_check(const char *tag, int cond) {
    la_checks++;
    if (!cond)
        la_failures++;
    print(tag, cond ? 1 : 0);
}

int main(void) {
    lifetime_owner o;
    lifetime_owner o2;
    lifetime_owner a;
    lifetime_owner c;
    lifetime_borrow b;
    lifetime_borrow b2;
    lifetime_token_t saved_token;
    lifetime_token_t ta;
    void *saved_ptr;
    size_t live_before;
    unsigned char src[4];
    unsigned char out[4];
    error err = error();

    src[0] = 1u; src[1] = 2u; src[2] = 3u; src[3] = 4u;

    stdlib_set_error_callback((stdlib_error_callback)0);
    o.ptr = (void *)0; o.token = LIFETIME_NO_TOKEN;
    o2.ptr = (void *)0; o2.token = LIFETIME_NO_TOKEN;
    a.ptr = (void *)0; a.token = LIFETIME_NO_TOKEN;
    c.ptr = (void *)0; c.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0; b.token = LIFETIME_NO_TOKEN;
    b2.ptr = (void *)0; b2.token = LIFETIME_NO_TOKEN;
    live_before = stdlib_state.meta_live;

    /* ---- A: valid borrow: guarded write/read through a live token ------ */
    la_check("la_init", lifetime_owner_init(&o, 16u, &err) == STDLIB_OK &&
                        !err.failed_now() && o.ptr != (void *)0 &&
                        o.token != LIFETIME_NO_TOKEN);
    la_check("la_borrow", lifetime_borrow_from(&b, &o, &err) == STDLIB_OK &&
                          !err.failed_now() && lifetime_borrow_is_live(&b) &&
                          b.token == o.token && b.ptr == o.ptr);
    la_check("la_write", lifetime_write(&b, 0u, src, 4u, &err) == STDLIB_OK &&
                         !err.failed_now());
    out[0] = 0u; out[1] = 0u; out[2] = 0u; out[3] = 0u;
    la_check("la_read", lifetime_read(&b, 0u, out, 4u, &err) == STDLIB_OK &&
                        !err.failed_now() && out[0] == 1u && out[1] == 2u &&
                        out[2] == 3u && out[3] == 4u);
    error_reset(&err);
    la_check("la_range_refused",
             lifetime_read(&b, 14u, out, 4u, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_BOUNDS &&
                 err.code() == CPLUS_EXIT_BOUNDS && out[0] == 1u);

    /* ---- B: stale refusal via metadata, never dereference stale pointer */
    saved_token = b.token;
    saved_ptr = b.ptr;
    error_reset(&err);
    la_check("la_release", lifetime_owner_release(&o, &err) == STDLIB_OK &&
                           !err.failed_now() && o.ptr == (void *)0 &&
                           o.token == LIFETIME_NO_TOKEN);
    /* The metadata slot is retired (generation 0) but the borrow descriptor
     * still carries the OLD token; validation must refuse before any access. */
    la_check("la_token_retired",
             stdlib_alloc_generation(saved_ptr) == LIFETIME_NO_TOKEN &&
                 b.token == saved_token && saved_token != LIFETIME_NO_TOKEN);
    la_check("la_stale_not_live", lifetime_borrow_is_live(&b) == 0);
    out[0] = 9u; out[1] = 9u; out[2] = 9u; out[3] = 9u;
    error_reset(&err);
    la_check("la_stale_read_refused",
             lifetime_read(&b, 0u, out, 4u, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                 err.code() == CPLUS_EXIT_USE_AFTER_FREE &&
                 out[0] == 9u && out[1] == 9u && out[2] == 9u && out[3] == 9u);
    error_reset(&err);
    la_check("la_stale_write_refused",
             lifetime_write(&b, 0u, src, 4u, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                 err.code() == CPLUS_EXIT_USE_AFTER_FREE &&
                 err.failed_now());
    /* A later allocation may reuse the same address; its generation differs,
     * so the old token is still refused (no false 'live' result). */
    error_reset(&err);
    la_check("la_reuse_owner",
             lifetime_owner_init(&o2, 16u, &err) == STDLIB_OK &&
                 !err.failed_now());
    out[0] = 9u;
    error_reset(&err);
    la_check("la_reuse_stale_refused",
             lifetime_borrow_is_live(&b) == 0 &&
                 lifetime_read(&b, 0u, out, 1u, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_LIFECYCLE && out[0] == 9u);
    error_reset(&err);
    la_check("la_release_o2", lifetime_owner_release(&o2, &err) == STDLIB_OK &&
                              !err.failed_now());

    /* ---- C: ownership transfer invalidates pre-transfer borrows -------- */
    error_reset(&err);
    la_check("la_init_a", lifetime_owner_init(&a, 16u, &err) == STDLIB_OK &&
                          !err.failed_now());
    la_check("la_borrow_a", lifetime_borrow_from(&b2, &a, &err) == STDLIB_OK &&
                            !err.failed_now());
    la_check("la_write_a", lifetime_write(&b2, 0u, src, 1u, &err) == STDLIB_OK &&
                           src[0] == 1u);
    ta = a.token;
    error_reset(&err);
    la_check("la_transfer", lifetime_transfer(&c, &a, &err) == STDLIB_OK &&
                            !err.failed_now());
    la_check("la_src_moved", a.ptr == (void *)0 &&
                             a.token == LIFETIME_NO_TOKEN);
    la_check("la_dst_token_new",
             c.ptr != (void *)0 && c.token != LIFETIME_NO_TOKEN &&
                 c.token != ta);
    out[0] = 0u;
    error_reset(&err);
    la_check("la_old_borrow_stale",
             lifetime_borrow_is_live(&b2) == 0 &&
                 lifetime_read(&b2, 0u, out, 1u, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_LIFECYCLE && out[0] == 0u);
    error_reset(&err);
    la_check("la_moved_from_no_borrow",
             lifetime_borrow_from(&b, &a, &err) == STDLIB_FAIL &&
                 err.failed_now());
    error_reset(&err);
    la_check("la_new_borrow_ok",
             lifetime_borrow_from(&b, &c, &err) == STDLIB_OK &&
                 lifetime_read(&b, 0u, out, 1u, &err) == STDLIB_OK &&
                 out[0] == 1u && !err.failed_now());

    /* ---- D: double release refusal ------------------------------------ */
    live_before = stdlib_state.meta_live;
    error_reset(&err);
    la_check("la_release_c", lifetime_owner_release(&c, &err) == STDLIB_OK &&
                             !err.failed_now() &&
                             stdlib_state.meta_live == live_before - 1u);
    error_reset(&err);
    la_check("la_double_release_refused",
             lifetime_owner_release(&c, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_OWNERSHIP &&
                 err.code() == CPLUS_EXIT_DOUBLE_FREE &&
                 stdlib_state.meta_live == live_before - 1u);
    error_reset(&err);
    la_check("la_release_moved_from_refused",
             lifetime_owner_release(&a, &err) == STDLIB_FAIL &&
                 err.code() == CPLUS_EXIT_DOUBLE_FREE &&
                 stdlib_state.meta_live == live_before - 1u);

    print("LIFETIME_ADAPT", la_checks, la_failures);
    return la_failures != 0;
}
