/* bounds_check.cp -- deterministic CLI over checked bounded queries.
 *
 * A DEMONSTRATION of runtime refusals from stdlib/memory.hp, not a memory
 * safety proof: the queries derive (base, size) from the checked allocator's
 * side table, compare integer addresses, and never dereference the inspected
 * block.  Every case is argv-selected and uses fixed sizes and offsets.
 *
 * Build (from repo root):
 *   ./build/c+ build examples/utilities/security_tools/bounds_check.cp \
 *     --strict --sanitize=none -I. -o examples/utilities/security_tools/bounds_check
 *
 * Usage:
 *   bounds_check exact | one-over | interior | stale
 *
 *   exact     [0,16) on a 16-byte tracked block          -> OK (0)
 *   one-over  [0,17) on a 16-byte tracked block          -> REFUSED 101/cat 5 (2)
 *   interior  interior pointer + explicit (base,size)    -> OK (0), 25 bytes refused
 *   stale     generation key captured before a transfer  -> REFUSED 103/cat 7 (2)
 */
#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static void bc_arg(char **argv, int i, const char **out) {
    const char *v = (const char *)0;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    *out = v;
}

static int bc_exact(void) {
    void *p = stdlib_alloc(16);
    const void *base = (const void *)0;
    size_t size = 0;
    unsigned long long key = 0;
    error e = error();
    int span_full = 0;
    int q_full = 0;

    if (p == (void *)0) {
        printf("BOUNDS exact REFUSED code=107 cat=4 reason=alloc-failed\n");
        return 1;
    }
    if (stdlib_mem_bounds(p, &base, &size) != STDLIB_OK) {
        printf("BOUNDS exact REFUSED code=101 cat=5 reason=no-metadata\n");
        stdlib_mem_release(&p);
        return 1;
    }
    key = stdlib_mem_token(p);
    span_full = stdlib_mem_span_ok(base, size, 0, 16);
    q_full = (stdlib_mem_query(p, 0, 16, key, &e) == STDLIB_OK);

    if (span_full && q_full)
        printf("BOUNDS exact OK cap=%zu span_full=%d query_full=%d\n",
               size, span_full, q_full);
    else
        printf("BOUNDS exact REFUSED code=101 cat=5 cap=%zu span_full=%d query_full=%d\n",
               size, span_full, q_full);
    stdlib_mem_release(&p);
    return (span_full && q_full) ? 0 : 1;
}

static int bc_one_over(void) {
    void *p = stdlib_alloc(16);
    const void *base = (const void *)0;
    size_t size = 0;
    unsigned long long key = 0;
    error e = error();
    int span_over = 0;
    int rc = STDLIB_OK;
    int refused = 0;

    if (p == (void *)0) {
        printf("BOUNDS one_over REFUSED code=107 cat=4 reason=alloc-failed\n");
        return 1;
    }
    (void)stdlib_mem_bounds(p, &base, &size);
    key = stdlib_mem_token(p);
    error_reset(&e);
    span_over = stdlib_mem_span_ok(base, size, 0, 17);
    rc = stdlib_mem_query(p, 0, 17, key, &e);
    refused = (!span_over && rc == STDLIB_FAIL && e.failed_now() == 1 &&
               e.category() == STDLIB_CATEGORY_BOUNDS &&
               e.code() == CPLUS_EXIT_BOUNDS);

    if (refused)
        printf("BOUNDS one_over REFUSED code=%d cat=%d span_1over=%d query=%d\n",
               e.code(), e.category(), span_over, rc);
    else
        printf("BOUNDS one_over UNEXPECTED_OK span_1over=%d query=%d cat=%d code=%d\n",
               span_over, rc, e.category(), e.code());
    stdlib_mem_release(&p);
    return refused ? 2 : 1;
}

static int bc_interior(void) {
    void *p = stdlib_alloc(32);
    const void *base = (const void *)0;
    size_t size = 0;
    const void *interior = (const void *)0;
    const void *ibase = (const void *)0;
    size_t isize = 0;
    int has_metadata = 0;
    int contains = 0;
    int over = 0;
    int ok = 0;

    if (p == (void *)0) {
        printf("BOUNDS interior REFUSED code=107 cat=4 reason=alloc-failed\n");
        return 1;
    }
    (void)stdlib_mem_bounds(p, &base, &size);
    interior = (const void *)((const unsigned char *)p + 8);
    has_metadata = (stdlib_mem_bounds(interior, &ibase, &isize) == STDLIB_OK);
    contains = stdlib_mem_span_contains(base, size, interior, 24);
    over = stdlib_mem_span_contains(base, size, interior, 25);
    ok = (!has_metadata && contains && !over);

    if (ok)
        printf("BOUNDS interior OK interior_has_metadata=%d contains_24=%d contains_25=%d\n",
               has_metadata, contains, over);
    else
        printf("BOUNDS interior REFUSED code=101 cat=5 interior_has_metadata=%d contains_24=%d contains_25=%d\n",
               has_metadata, contains, over);
    stdlib_mem_release(&p);
    return ok ? 0 : 1;
}

static int bc_stale(void) {
    void *p = stdlib_alloc(16);
    void *q = (void *)0;
    unsigned long long old_key = 0;
    error e = error();
    int moved = 0;
    int current = 0;
    int rc = STDLIB_OK;
    int old_code = 0;
    int old_cat = 0;
    int fresh = 0;
    int refused = 0;

    if (p == (void *)0) {
        printf("BOUNDS stale REFUSED code=107 cat=4 reason=alloc-failed\n");
        return 1;
    }
    old_key = stdlib_mem_token(p);
    error_reset(&e);
    moved = (stdlib_mem_transfer(&q, &p, &e) == STDLIB_OK &&
             p == (void *)0 && q != (void *)0);
    current = stdlib_mem_key_current(q, old_key);
    rc = stdlib_mem_query(q, 0, 16, old_key, &e);
    old_code = e.code();
    old_cat = e.category();
    refused = (moved && !current && rc == STDLIB_FAIL && e.failed_now() == 1 &&
               old_cat == STDLIB_CATEGORY_LIFECYCLE &&
               old_code == CPLUS_EXIT_USE_AFTER_FREE);
    error_reset(&e);
    fresh = (stdlib_mem_query(q, 0, 16, stdlib_mem_token(q), &e) == STDLIB_OK);

    if (refused && fresh)
        printf("BOUNDS stale REFUSED code=%d cat=%d moved=%d old_key_current=%d old_query=%d fresh_query=%d\n",
               old_code, old_cat, moved, current, rc, fresh);
    else
        printf("BOUNDS stale UNEXPECTED_OK moved=%d old_key_current=%d old_query=%d fresh=%d\n",
               moved, current, rc, fresh);
    stdlib_mem_release(&q);
    return (refused && fresh) ? 2 : 1;
}

int main(int argc, char **argv) {
    const char *which = (const char *)0;
    int rc;

    stdlib_set_error_callback((stdlib_error_callback)0);
    if (argc < 2)
        return 1;
    if (argc > 2)
        return 1;
    bc_arg(argv, 1, &which);
    if (which == (const char *)0)
        return 1;

    if (strcmp(which, "exact") == 0)
        rc = bc_exact();
    else if (strcmp(which, "one-over") == 0)
        rc = bc_one_over();
    else if (strcmp(which, "interior") == 0)
        rc = bc_interior();
    else if (strcmp(which, "stale") == 0)
        rc = bc_stale();
    else {
        printf("BOUNDS unknown_case REFUSED code=108 cat=1 reason=unknown-case\n");
        return 1;
    }
    fflush(stdout);
    return rc;
}
