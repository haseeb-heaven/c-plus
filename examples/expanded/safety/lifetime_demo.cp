/* Safety demo 3: token-based lifetime/borrow adaptation in
 * stdlib/lifetime_adapt.hp.
 *
 * A borrow carries a raw pointer plus a generation token captured from its
 * owner.  The guarded accessors validate the token against the stdlib metadata
 * BEFORE touching the target, so a stale borrow is refused without ever being
 * dereferenced.  Releasing an owner twice is refused with a double-free code.
 * All outcomes are observed at run time; this is refusal evidence, not a
 * lifetime-safety proof.
 *
 * Build (from repo root):
 *   ./build/c+ build examples/expanded/safety/lifetime_demo.cp \
 *     --strict --sanitize=none -I. -o examples/expanded/safety/lifetime_demo
 */
#include <stdio.h>
#include <string.h>

#include "stdlib/lifetime_adapt.hp"

static int g_failed = 0;

static void mark(const char *case_name, const char *got, const char *want,
                 const char *detail) {
    printf("lifetime %s %s %s\n", case_name, got, detail);
    if (strcmp(got, want) != 0)
        g_failed = 1;
    fflush(stdout);
}

static int cat_is(error *e, int category, int code) {
    return e->failed_now() == 1 && e->category() == category &&
           e->code() == code;
}

int main(void) {
    lifetime_owner o;
    lifetime_borrow b;
    unsigned char src[4] = {11, 22, 33, 44};
    unsigned char out[4] = {99, 99, 99, 99};
    lifetime_token_t saved;
    void *saved_ptr;
    size_t live_before;
    error err = error();
    char detail[160];

    stdlib_set_error_callback((stdlib_error_callback)0);
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;

    /* ---- valid borrow: guarded write/read through a live token ----------- */
    {
        int init = lifetime_owner_init(&o, 16u, &err) == STDLIB_OK;
        int borrow = lifetime_borrow_from(&b, &o, &err) == STDLIB_OK;
        int live = lifetime_borrow_is_live(&b);
        int write = lifetime_write(&b, 0u, src, 4u, &err) == STDLIB_OK;
        int read;
        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
        read = lifetime_read(&b, 0u, out, 4u, &err) == STDLIB_OK &&
               out[0] == 11 && out[1] == 22 && out[2] == 33 && out[3] == 44;
        snprintf(detail, sizeof(detail),
                 "init=%d borrow=%d live=%d write=%d read_exact=%d",
                 init, borrow, live, write, read);
        mark("valid_borrow", (init && borrow && live && write && read)
                 ? "OK" : "REFUSED", "OK", detail);
    }

    /* ---- stale token: release the owner, then try to use the old borrow --
     * The borrow descriptor still holds the OLD token and the OLD pointer.
     * lifetime_read/write reject it from metadata only; the 0x99 sentinel in
     * `out` proves no byte of the released block was ever read. */
    saved = b.token;
    saved_ptr = b.ptr;
    error_reset(&err);
    {
        int released = lifetime_owner_release(&o, &err) == STDLIB_OK;
        int retired = (stdlib_alloc_generation(saved_ptr) == LIFETIME_NO_TOKEN &&
                       b.token == saved && saved != LIFETIME_NO_TOKEN);
        int not_live = (lifetime_borrow_is_live(&b) == 0);
        /* Restore the sentinel AFTER the valid read so a stale read that
         * wrongly dereferences the block would be visible here. */
        out[0] = 0x99; out[1] = 0x99; out[2] = 0x99; out[3] = 0x99;
        int read_rc = lifetime_read(&b, 0u, out, 4u, &err);
        int read_cat = cat_is(&err, STDLIB_CATEGORY_LIFECYCLE,
                              CPLUS_EXIT_USE_AFTER_FREE);
        int untouched = (out[0] == 0x99 && out[1] == 0x99 &&
                         out[2] == 0x99 && out[3] == 0x99);
        error_reset(&err);
        int write_rc = lifetime_write(&b, 0u, src, 4u, &err);
        int write_cat = cat_is(&err, STDLIB_CATEGORY_LIFECYCLE,
                               CPLUS_EXIT_USE_AFTER_FREE);
        snprintf(detail, sizeof(detail),
                 "released=%d retired=%d not_live=%d read=%d cat=%d untouched=%d write=%d wcat=%d",
                 released, retired, not_live, read_rc, read_cat, untouched,
                 write_rc, write_cat);
        mark("stale_token_refused",
             (released && retired && not_live && read_rc == STDLIB_FAIL &&
              read_cat && untouched && write_rc == STDLIB_FAIL && write_cat)
                 ? "REFUSED" : "OK",
             "REFUSED", detail);
    }

    /* ---- double release: the second call is refused, no allocator call --- */
    error_reset(&err);
    {
        int init2 = lifetime_owner_init(&o, 8u, &err) == STDLIB_OK;
        live_before = stdlib_state.meta_live;
        error_reset(&err);
        int first = lifetime_owner_release(&o, &err) == STDLIB_OK;
        error_reset(&err);
        int second = lifetime_owner_release(&o, &err);
        int dbl_cat = cat_is(&err, STDLIB_CATEGORY_OWNERSHIP,
                             CPLUS_EXIT_DOUBLE_FREE);
        int stable = (stdlib_state.meta_live == live_before - 1u);
        snprintf(detail, sizeof(detail),
                 "init=%d first=%d second=%d double_free_cat=%d live_stable=%d",
                 init2, first, second, dbl_cat, stable);
        mark("double_release_refused",
             (init2 && first && second == STDLIB_FAIL && dbl_cat && stable)
                 ? "REFUSED" : "OK",
             "REFUSED", detail);
    }

    return g_failed;
}
