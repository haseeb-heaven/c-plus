/* 100% public-method test for stdlib/lifetime_adapt.hp.
 *
 * Every callable in the header is invoked by a named case, its result is
 * verified, and one line "<header> <method> PASS|FAIL" is printed per method.
 * Deterministic: no stdin, no wall clock, no external network, and no
 * dereference of a stale pointer.  Exits non-zero iff any case fails.
 */
#include <stddef.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/lifetime_adapt.hp"

static int mt_total = 0;
static int mt_passed = 0;
static int mt_failed = 0;

static void mt_report(const char *method, int ok) {
    mt_total++;
    if (ok) mt_passed++; else mt_failed++;
    print("lifetime_adapt.hp", method, ok ? "PASS" : "FAIL");
}

int main(void) {
    error err = error();
    lifetime_owner o;
    lifetime_owner o2;
    lifetime_owner a;
    lifetime_owner c;
    lifetime_borrow b;
    lifetime_borrow b2;
    lifetime_token_t ta;
    size_t live_before;
    unsigned char src[4];
    unsigned char out[4];
    int ok;

    src[0] = 1u; src[1] = 2u; src[2] = 3u; src[3] = 4u;
    stdlib_set_error_callback((stdlib_error_callback)0);
    o.ptr = (void *)0; o.token = LIFETIME_NO_TOKEN;
    o2.ptr = (void *)0; o2.token = LIFETIME_NO_TOKEN;
    a.ptr = (void *)0; a.token = LIFETIME_NO_TOKEN;
    c.ptr = (void *)0; c.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0; b.token = LIFETIME_NO_TOKEN;
    b2.ptr = (void *)0; b2.token = LIFETIME_NO_TOKEN;

    /* 1. lifetime_owner_init: success + NULL owner + already-held owner */
    error_reset(&err);
    ok = lifetime_owner_init(&o, 16u, &err) == STDLIB_OK && err.ok() &&
         o.ptr != (void *)0 && o.token != LIFETIME_NO_TOKEN;
    error_reset(&err);
    ok = ok && lifetime_owner_init((lifetime_owner *)0, 8u, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    error_reset(&err);
    ok = ok && lifetime_owner_init(&o, 8u, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_OWNERSHIP;
    mt_report("lifetime_owner_init", ok);

    /* 2. lifetime_check: live token, NULL ptr, zero token */
    error_reset(&err);
    ok = lifetime_check(o.ptr, o.token, &err) == STDLIB_OK && err.ok();
    error_reset(&err);
    ok = ok && lifetime_check((void *)0, o.token, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_LIFECYCLE &&
         err.code() == CPLUS_EXIT_USE_AFTER_FREE;
    error_reset(&err);
    ok = ok && lifetime_check(o.ptr, LIFETIME_NO_TOKEN, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_LIFECYCLE;
    mt_report("lifetime_check", ok);

    /* 3. lifetime_borrow_from: live borrow + NULL descriptors */
    error_reset(&err);
    ok = lifetime_borrow_from(&b, &o, &err) == STDLIB_OK && err.ok() &&
         b.ptr == o.ptr && b.token == o.token;
    error_reset(&err);
    ok = ok && lifetime_borrow_from((lifetime_borrow *)0, &o, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    error_reset(&err);
    ok = ok && lifetime_borrow_from(&b2, (const lifetime_owner *)0, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    mt_report("lifetime_borrow_from", ok);

    /* 4. lifetime_borrow_is_live: live, NULL ptr, NULL descriptor */
    ok = lifetime_borrow_is_live(&b) == 1;
    b2.ptr = (void *)0; b2.token = o.token;
    ok = ok && lifetime_borrow_is_live(&b2) == 0;
    ok = ok && lifetime_borrow_is_live((const lifetime_borrow *)0) == 0;
    mt_report("lifetime_borrow_is_live", ok);

    /* 5. lifetime_write: live write, bounds/NULL refusal, stale-token refusal
     *    (the stale borrow comes from a second owner that is released here) */
    error_reset(&err);
    ok = lifetime_write(&b, 0u, src, 4u, &err) == STDLIB_OK && err.ok();
    error_reset(&err);
    ok = ok && lifetime_write(&b, 14u, src, 4u, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_BOUNDS && err.code() == CPLUS_EXIT_BOUNDS;
    error_reset(&err);
    ok = ok && lifetime_write(&b, 0u, (const void *)0, 4u, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    error_reset(&err);
    ok = ok && lifetime_owner_init(&o2, 8u, &err) == STDLIB_OK &&
         lifetime_borrow_from(&b2, &o2, &err) == STDLIB_OK &&
         lifetime_owner_release(&o2, &err) == STDLIB_OK;
    error_reset(&err);
    ok = ok && lifetime_write(&b2, 0u, src, 1u, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_LIFECYCLE &&
         err.code() == CPLUS_EXIT_USE_AFTER_FREE;
    mt_report("lifetime_write", ok);

    /* 6. lifetime_read: live read, bounds/NULL refusal, stale-token refusal
     *    with proof that the destination is not touched on refusal */
    out[0] = 0u; out[1] = 0u; out[2] = 0u; out[3] = 0u;
    error_reset(&err);
    ok = lifetime_read(&b, 0u, out, 4u, &err) == STDLIB_OK && err.ok() &&
         out[0] == 1u && out[1] == 2u && out[2] == 3u && out[3] == 4u;
    out[0] = 9u; out[1] = 9u; out[2] = 9u; out[3] = 9u;
    error_reset(&err);
    ok = ok && lifetime_read(&b, 14u, out, 4u, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_BOUNDS &&
         out[0] == 9u && out[1] == 9u && out[2] == 9u && out[3] == 9u;
    error_reset(&err);
    ok = ok && lifetime_read(&b, 0u, (void *)0, 4u, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    error_reset(&err);
    ok = ok && lifetime_read(&b2, 0u, out, 1u, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_LIFECYCLE &&
         err.code() == CPLUS_EXIT_USE_AFTER_FREE && out[0] == 9u;
    mt_report("lifetime_read", ok);

    /* 7. lifetime_owner_release: success and double-release refusal */
    error_reset(&err);
    ok = lifetime_owner_release(&o, &err) == STDLIB_OK && err.ok() &&
         o.ptr == (void *)0 && o.token == LIFETIME_NO_TOKEN;
    error_reset(&err);
    ok = ok && lifetime_owner_release(&o, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_OWNERSHIP &&
         err.code() == CPLUS_EXIT_DOUBLE_FREE;
    mt_report("lifetime_owner_release", ok);

    /* 8. lifetime_transfer: dst token fresh, src moved, old borrow stale,
     *    invalid transfer and moved-from borrow refusal */
    error_reset(&err);
    ok = lifetime_owner_init(&a, 16u, &err) == STDLIB_OK && err.ok();
    error_reset(&err);
    ok = ok && lifetime_borrow_from(&b, &a, &err) == STDLIB_OK;
    ta = a.token;
    error_reset(&err);
    ok = ok && lifetime_transfer(&c, &a, &err) == STDLIB_OK && err.ok() &&
         a.ptr == (void *)0 && a.token == LIFETIME_NO_TOKEN &&
         c.ptr != (void *)0 && c.token != LIFETIME_NO_TOKEN && c.token != ta;
    ok = ok && lifetime_borrow_is_live(&b) == 0;
    error_reset(&err);
    /* moved-from src is refused by lifetime_check -> LIFECYCLE category */
    ok = ok && lifetime_transfer(&o, &a, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_LIFECYCLE;
    error_reset(&err);
    ok = ok && lifetime_borrow_from(&b, &a, &err) == STDLIB_FAIL && err.failed_now();
    error_reset(&err);
    ok = ok && lifetime_borrow_from(&b, &c, &err) == STDLIB_OK &&
         lifetime_read(&b, 0u, out, 1u, &err) == STDLIB_OK && err.ok();
    mt_report("lifetime_transfer", ok);

    /* verify release of the transferred owner with metadata accounting */
    live_before = stdlib_state.meta_live;
    error_reset(&err);
    ok = lifetime_owner_release(&c, &err) == STDLIB_OK && err.ok() &&
         stdlib_state.meta_live == live_before - 1u;

    print("METHOD_TEST lifetime_adapt.hp", mt_total, mt_passed, mt_failed, 0);
    return mt_failed != 0 || !ok;
}
