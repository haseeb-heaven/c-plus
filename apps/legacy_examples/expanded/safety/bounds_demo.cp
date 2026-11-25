/* Safety demo 2: bounded query helpers in stdlib/memory.hp.
 *
 * These are PURE predicates/queries: they derive (base, size) from the
 * stdlib metadata side table and check [off, off+n) with a non-wrapping
 * subtraction.  Nothing is dereferenced and nothing is written here.  Every
 * outcome is observed at run time; this is refusal evidence, not a proof of
 * memory safety.
 *
 * Cases: exact-fit OK, one-over REFUSED, interior pointer OK (with explicit
 * base), stale generation key REFUSED (after a transfer bumps the generation).
 *
 * Build (from repo root):
 *   ./build/c+ build apps/legacy_examples/expanded/safety/bounds_demo.cp \
 *     --strict --sanitize=none -I. -o apps/legacy_examples/expanded/safety/bounds_demo
 */
#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static int g_failed = 0;

static void mark(const char *case_name, const char *got, const char *want,
                 const char *detail) {
    printf("bounds %s %s %s\n", case_name, got, detail);
    if (strcmp(got, want) != 0)
        g_failed = 1;
    fflush(stdout);
}

static int cat_is(error *e, int category, int code) {
    return e->failed_now() == 1 && e->category() == category &&
           e->code() == code;
}

int main(void) {
    error e = error();
    void *p;
    void *q;
    const void *base = (const void *)0;
    size_t size = 0;
    unsigned long long key;
    char detail[160];

    stdlib_set_error_callback((stdlib_error_callback)0);

    /* ---- exact fit: full and interior subranges are accepted ------------- */
    p = stdlib_alloc(16);
    if (p == (void *)0) {
        mark("exact_fit", "REFUSED", "OK", "stdlib_alloc(16) failed");
        return g_failed;
    }
    (void)stdlib_mem_bounds(p, &base, &size);
    key = stdlib_mem_token(p);
    {
        int span_full = stdlib_mem_span_ok(base, size, 0, 16);
        int span_in = stdlib_mem_span_ok(base, size, 4, 12);
        int q_full = stdlib_mem_query(p, 0, 16, key, &e) == STDLIB_OK;
        int q_in = stdlib_mem_query(p, 4, 12, key, &e) == STDLIB_OK;
        snprintf(detail, sizeof(detail),
                 "cap=%zu span_full=%d span_interior=%d query_full=%d query_interior=%d",
                 size, span_full, span_in, q_full, q_in);
        mark("exact_fit", span_full && span_in && q_full && q_in
                 ? "OK" : "REFUSED", "OK", detail);
    }

    /* ---- one over: spatial predicate and combined query both refuse ----- */
    error_reset(&e);
    {
        int span_over = stdlib_mem_span_ok(base, size, 0, 17);
        int span_tail = stdlib_mem_span_ok(base, size, 13, 4);
        int rc = stdlib_mem_query(p, 0, 17, key, &e);
        int cat_ok = cat_is(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS);
        snprintf(detail, sizeof(detail),
                 "span_1over=%d span_tail=%d query=%d bounds_cat=%d",
                 span_over, span_tail, rc, cat_ok);
        mark("one_over", (!span_over && !span_tail && rc == STDLIB_FAIL &&
                          cat_ok) ? "REFUSED" : "OK", "REFUSED", detail);
    }
    stdlib_mem_release(&p);

    /* ---- interior pointer: containment OK with an explicit (base,size) --- */
    p = stdlib_alloc(32);
    if (p == (void *)0) {
        mark("interior_pointer", "REFUSED", "OK", "stdlib_alloc(32) failed");
        return g_failed;
    }
    (void)stdlib_mem_bounds(p, &base, &size);
    {
        const void *ibase = (const void *)0;
        size_t isize = 0;
        int interior_tracked = (stdlib_mem_bounds((const void *)((unsigned char *)p + 8),
                                                  &ibase, &isize) == 0);
        int contains = stdlib_mem_span_contains(base, size,
                                                (const void *)((unsigned char *)p + 8), 24);
        int over = stdlib_mem_span_contains(base, size,
                                            (const void *)((unsigned char *)p + 8), 25);
        snprintf(detail, sizeof(detail),
                 "interior_has_metadata=%d contains_24=%d contains_25=%d",
                 interior_tracked, contains, over);
        mark("interior_pointer", (!interior_tracked && contains && !over)
                 ? "OK" : "REFUSED", "OK", detail);
    }
    stdlib_mem_release(&p);

    /* ---- stale generation: transfer bumps the key, old key is refused --- */
    p = stdlib_alloc(16);
    if (p == (void *)0) {
        mark("stale_generation", "REFUSED", "OK", "stdlib_alloc(16) failed");
        return g_failed;
    }
    key = stdlib_mem_token(p);
    q = (void *)0;
    error_reset(&e);
    {
        int moved = stdlib_mem_transfer(&q, &p, &e) == STDLIB_OK &&
                    p == (void *)0 && q != (void *)0;
        int current = stdlib_mem_key_current(q, key);
        int rc = stdlib_mem_query(q, 0, 16, key, &e);
        int cat_ok = cat_is(&e, STDLIB_CATEGORY_LIFECYCLE,
                            CPLUS_EXIT_USE_AFTER_FREE);
        error_reset(&e);
        int fresh = stdlib_mem_query(q, 0, 16, stdlib_mem_token(q), &e) == STDLIB_OK;
        snprintf(detail, sizeof(detail),
                 "moved=%d old_key_current=%d old_query=%d lifecycle_cat=%d fresh_query=%d",
                 moved, current, rc, cat_ok, fresh);
        mark("stale_generation", (moved && !current && rc == STDLIB_FAIL &&
                                  cat_ok && fresh) ? "REFUSED" : "OK",
             "REFUSED", detail);
    }
    stdlib_mem_release(&q);

    return g_failed;
}
