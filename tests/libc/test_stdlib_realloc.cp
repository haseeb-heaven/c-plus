/* Regression: stdlib_realloc must keep the checked side table in sync.
 * Built by c+ (stdlib/error.hp is a C+ header). Failure injection for the
 * underlying cplus_realloc is covered by tests/crt/unit/test_alloc_fault_matrix.c. */
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int checks = 0;
static int failures = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) { failures++; printf("FAIL %s\n", tag); }
    else printf("ok %s\n", tag);
}

int main(void) {
    void *p = stdlib_alloc(32);
    void *q;
    unsigned long long g0, g1;
    stdlib_alloc_fail_reset();

    ck(p != NULL && stdlib_alloc_size(p) == 32, "alloc_initial");
    g0 = stdlib_alloc_generation(p);
    ck(g0 != 0, "generation_initial");

    q = stdlib_realloc(p, 64);
    ck(q != NULL, "realloc_grow_ok");
    g1 = stdlib_alloc_generation(q);
    ck(stdlib_alloc_size(q) == 64, "metadata_new_size");
    ck(stdlib_alloc_size(p) == (p == q ? 64u : 0u), "metadata_old_identity");
    ck(g1 > g0, "generation_bumped");
    ck(stdlib_alloc_check_generation(q, g0) == 0, "stale_generation_refused");

    /* Zero-size normalises to one backing byte, like stdlib_alloc. */
    p = stdlib_realloc(q, 0);
    ck(p != NULL && stdlib_alloc_size(p) == 1, "zero_size_normalised");

    /* Untracked pointer and overflow are refused without touching state. */
    {
        int stack = 0;
        ck(stdlib_realloc(&stack, 8) == NULL, "untracked_pointer_refused");
        ck(stdlib_realloc(p, STDLIB_ALLOC_LIMIT + 1) == NULL, "overflow_refused");
        ck(stdlib_alloc_size(p) == 1, "metadata_preserved_on_refusal");
    }

    stdlib_release(p);
    ck(stdlib_alloc_size(p) == 0, "release_retires_metadata");
    printf("stdlib_realloc: %d/%d checks ok\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
