/* PR31 fixture driver: orchestrates cross-TU ownership, ABI and error checks.
 *
 * Plain C11.  The final line is consumed by run.sh:
 *   PR31_FIXTURE_RESULT <pass> <fail>
 */
#include "pr31_shared.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass;
static int g_fail;

static void ck(const char *name, int ok)
{
    printf("PR31_FIXTURE %s %s\n", name, ok ? "PASS" : "FAIL");
    if (ok)
        ++g_pass;
    else
        ++g_fail;
}

static void supported_pairings(void)
{
    void *p, *slot;
    int rc, str_ok;

    /* A allocates, B (this TU and tu_b.c) releases. */
    p = pr31_a_malloc(64);
    slot = p;
    rc = pr31_b_release(&slot);
    ck("A_malloc__B_checked_free", p != NULL && rc == 0 && slot == NULL);

    p = pr31_a_calloc(4, 16);
    slot = p;
    rc = cplus_free_checked(&slot);
    ck("A_calloc__B_free_checked", p != NULL && rc == 0 && slot == NULL);

    p = pr31_a_realloc(NULL, 72);
    slot = p;
    rc = cplus_free_checked(&slot);
    ck("A_realloc__B_free_checked", p != NULL && rc == 0 && slot == NULL);

    p = pr31_a_aligned_alloc(16, 64);
    slot = p;
    rc = cplus_alloc_checked_free(&slot);
    ck("A_aligned_alloc__B_checked_free",
       p != NULL && ((uintptr_t)p % 16u) == 0u && rc == 0 && slot == NULL);

    p = pr31_a_strdup("cross-tu");
    str_ok = p != NULL && strcmp((const char *)p, "cross-tu") == 0;
    slot = p;
    rc = cplus_alloc_checked_free(&slot);
    ck("A_strdup__B_checked_free", str_ok && rc == 0 && slot == NULL);

    p = pr31_a_strndup("abcdef", 3);
    str_ok = p != NULL && strcmp((const char *)p, "abc") == 0;
    slot = p;
    rc = cplus_free_checked(&slot);
    ck("A_strndup__B_free_checked", str_ok && rc == 0 && slot == NULL);

    /* B allocates, A releases (the reverse direction). */
    p = cplus_malloc(64);
    slot = p;
    rc = pr31_a_release_checked(&slot);
    ck("B_malloc__A_checked_free", p != NULL && rc == 0 && slot == NULL);

    p = cplus_calloc(4, 8);
    slot = p;
    rc = pr31_a_release_fc(&slot);
    ck("B_calloc__A_free_checked", p != NULL && rc == 0 && slot == NULL);

    p = cplus_aligned_alloc(16, 32);
    pr31_a_release(p);
    ck("B_aligned_alloc__A_free", p != NULL && pr31_a_is_live(p) == 0);

    p = pr31_a_malloc(32);
    cplus_free(p);
    ck("A_malloc__B_raw_cplus_free", p != NULL && pr31_a_is_live(p) == 0);
}

static void mismatch_and_detection(void)
{
    void *foreign, *slot, *p, *saved;
    unsigned long inv0, dbl0, cnt0, cnt1;
    int rc, rc2, live_before, live_after;

    /* Foreign host block -> cplus checked free: refused, EINVAL, counted.
     * The block is later released with its real host allocator, so the test
     * leaks nothing and leaves no stale metadata. */
    foreign = malloc(32);
    slot = foreign;
    inv0 = pr31_a_invalid_frees();
    dbl0 = pr31_a_double_frees();
    cnt0 = inv0 + dbl0;
    errno = 0;
    rc = cplus_free_checked(&slot);
    cnt1 = pr31_a_invalid_frees() + pr31_a_double_frees();
    /* The side table classifies an untracked address as "invalid" unless the
     * address was recently freed, in which case it is counted as "double".
     * Either way the free is refused and exactly one counter advances. */
    ck("foreign_host_block_refused",
       foreign != NULL && rc == EINVAL && errno == EINVAL &&
       slot == NULL && cnt1 == cnt0 + 1u);
    free(foreign);

    /* cplus block released by the host allocator: the host CRT accepts it
     * (same backend) but the side table is NOT updated.  This is the case the
     * wrapper cannot detect after the fact; it is explicitly prohibited. */
    p = pr31_a_malloc(64);
    live_before = pr31_a_is_live(p);
    free(p);
    live_after = pr31_a_is_live(p);
    ck("cplus_to_host_free_leaves_stale_metadata",
       live_before == 1 && live_after == 1);

    /* Same undetected-staleness result for cplus_aligned_alloc on POSIX. */
    p = pr31_a_aligned_alloc(16, 64);
    live_before = pr31_a_is_live(p);
    free(p);
    live_after = pr31_a_is_live(p);
    ck("aligned_to_host_free_leaves_stale_metadata",
       p != NULL && live_before == 1 && live_after == 1);

    /* Hardened checked free refuses a stale pointer before the raw wrapper
     * (and therefore the host allocator) is reached: *p is left untouched. */
    p = pr31_a_malloc(32);
    saved = p;
    dbl0 = pr31_a_double_frees();
    rc = cplus_alloc_checked_free(&p);
    p = saved;
    errno = 0;
    rc2 = cplus_alloc_checked_free(&p);
    ck("double_free_refused",
       rc == 0 && rc2 == EINVAL && errno == EINVAL && p == saved &&
       pr31_a_double_frees() == dbl0);

    /* The raw wrapper itself does advance the allocator's double-free counter
     * when handed an address from its recent-free table. */
    p = pr31_a_malloc(32);
    saved = p;
    dbl0 = pr31_a_double_frees();
    rc = cplus_free_checked(&p);
    p = saved;
    errno = 0;
    rc2 = cplus_free_checked(&p);
    ck("raw_double_free_counted",
       rc == 0 && rc2 == EINVAL && errno == EINVAL &&
       pr31_a_double_frees() == dbl0 + 1u);

    /* Explicit trusted contract: a host block registered in the side table
     * becomes releasable by the checked wrapper. */
    p = malloc(48);
    if (p != NULL)
        memset(p, 0, 48);
    rc = pr31_a_register(p, 48);
    errno = 0;
    rc2 = cplus_alloc_checked_free(&p);
    ck("registered_host_block_released", rc == 1 && rc2 == 0 && p == NULL);

    errno = 0;
    p = NULL;
    ck("checked_free_null_refused",
       cplus_alloc_checked_free(&p) == EINVAL && errno == EINVAL);
    cplus_free(NULL); /* documented no-op; reaching here proves no crash */
    ck("raw_free_null_is_noop", 1);
}

static void metadata_and_ownership(void)
{
    cplus_alloc_meta m;
    void *p, *q = NULL, *slot;
    unsigned long long g0;
    int rc;

    p = pr31_a_calloc(8, 16);
    slot = p;
    rc = cplus_alloc_get_meta(p, &m);
    ck("metadata_across_tu",
       rc == 0 && m.is_live == 1 && m.byte_size == 128 &&
       m.elem_size == 16 && m.elem_count == 8 &&
       cplus_alloc_size(p) == 128 && cplus_alloc_elem_size(p) == 16 &&
       cplus_alloc_length(p) == 8 &&
       cplus_alloc_remaining_capacity(p) == 128);
    {
        size_t n = 0;
        ck("query_length_across_tu",
           cplus_alloc_query_length(p, 16, &n) == 0 && n == 8);
    }
    rc = cplus_alloc_checked_free(&slot);
    ck("metadata_block_released", rc == 0 && slot == NULL);

    /* Ownership moves A -> B; generation must change and the old slot must
     * be emptied (a second transfer is refused). */
    p = pr31_a_malloc(48);
    rc = cplus_alloc_get_meta(p, &m);
    g0 = m.generation;
    pr31_a_set_transfer_src(p);
    rc = pr31_a_transfer_to(&q);
    {
        cplus_alloc_meta m2;
        int rc2 = cplus_alloc_get_meta(q, &m2);
        ck("ownership_transfer_across_tu",
           rc == 0 && q == p && rc2 == 0 && m2.generation != g0);
    }
    {
        void *dummy = NULL;
        ck("transfer_from_empty_slot_refused",
           pr31_a_transfer_to(&dummy) == EINVAL && dummy == NULL);
    }
    slot = q;
    rc = cplus_alloc_checked_free(&slot);
    ck("transferred_block_released", rc == 0 && slot == NULL);
}

static void atomic_and_errno(void)
{
    void *p, *saved;
    cplus_alloc_meta before, after;
    int rc;

    p = cplus_malloc(64);
    saved = p;
    (void)cplus_alloc_get_meta(p, &before);
    pr31_a_fail_after(0);
    errno = 0;
    rc = pr31_a_atomic_realloc(&p, 128);
    (void)cplus_alloc_get_meta(p, &after);
    ck("atomic_realloc_failure_is_atomic",
       rc == ENOMEM && errno == ENOMEM && p == saved &&
       after.byte_size == before.byte_size &&
       after.generation == before.generation);
    pr31_a_fail_reset();
    rc = pr31_a_atomic_realloc(&p, 128);
    ck("atomic_realloc_success",
       rc == 0 && p != NULL && cplus_alloc_size(p) == 128);
    errno = 0;
    rc = cplus_alloc_checked_free(&p);
    ck("atomic_realloc_block_released", rc == 0 && errno == 0);

    pr31_b_set_errno(EDOM);
    ck("errno_propagates_across_tu", pr31_a_get_errno() == EDOM);

    pr31_a_fail_after(0);
    errno = 0;
    p = cplus_malloc(16);
    ck("allocator_failure_errno_across_tu", p == NULL && errno == ENOMEM);
    pr31_a_fail_reset();
}

static void callbacks_layout_and_opaque(void)
{
    pr31_vsum_t fp;
    pr31_record r;
    pr31_union u;
    pr31_handle h;
    void *slot;
    int rc;

    fp = pr31_a_vsum_ptr();
    ck("function_pointer_returned_across_tu", fp != NULL && fp(4, 1, 2, 3, 4) == 10);
    ck("callback_into_consumer_tu", pr31_a_call_cb(pr31_b_add, 20, 22) == 42);
    pr31_a_set_vlist_cb(pr31_b_vlist_sum);
    ck("va_list_forwarded_across_tu", pr31_a_call_vlist_cb(3, 5, 6, 7) == 18);

    pr31_a_fill_union(&u);
    pr31_a_fill_record(&r, &u);
    ck("struct_union_layout_across_tu", pr31_b_check_record(&r, &u) == 1);

    h = pr31_a_make_handle(4242);
    ck("opaque_handle_roundtrip", h != NULL && pr31_b_use_handle(h, 4242) == 1);
    slot = h;
    rc = cplus_alloc_checked_free(&slot);
    ck("opaque_handle_released", rc == 0 && slot == NULL);
}

int main(void)
{
    supported_pairings();
    mismatch_and_detection();
    metadata_and_ownership();
    atomic_and_errno();
    callbacks_layout_and_opaque();
    printf("PR31_FIXTURE_RESULT %d %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
