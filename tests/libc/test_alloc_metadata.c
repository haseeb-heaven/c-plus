/* Allocation metadata, safe length query, ownership transfer, failure
 * injection, double-free/invalid-free diagnostics, and CPLUS_MALLOC_TRACKING
 * coverage for cplus_* allocator primitives. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "install-include/cplus_libc.h"

int main(void) {
    cplus_alloc_meta meta;
    size_t count = 0;
    size_t base_live = cplus_alloc_live_count();
    size_t base_bytes = cplus_alloc_live_bytes();

    /* 1. cplus_malloc + metadata */
    void *p = cplus_malloc(32);
    assert(p != NULL);
    assert(cplus_alloc_is_live(p) == 1);
    assert(cplus_alloc_size(p) == 32);
    assert(cplus_alloc_elem_size(p) == 1);
    assert(cplus_alloc_length(p) == 32);
    assert(cplus_alloc_get_meta(p, &meta) == 0);
    assert(meta.is_live == 1 && meta.byte_size == 32 && meta.elem_size == 1 && meta.elem_count == 32);
    assert(meta.generation > 0);
    assert(cplus_alloc_check_generation(p, meta.generation) == 1);
    assert(cplus_alloc_check_generation(p, meta.generation + 1) == 0);
    assert(cplus_alloc_query_length(p, sizeof(int), &count) == 0 && count == 32 / sizeof(int));
    assert(cplus_alloc_live_count() == base_live + 1);
    assert(cplus_alloc_live_bytes() == base_bytes + 32);

    /* Interior/untracked pointers must not claim live metadata */
    assert(cplus_alloc_is_live((char *)p + 1) == 0);
    assert(cplus_alloc_size((char *)p + 1) == 0);
    assert(cplus_alloc_query_length((char *)p + 1, 1, &count) != 0 && count == 0);

    /* 2. Ownership transfer bumps generation and clears source */
    void *moved = NULL;
    unsigned long long gen_before = meta.generation;
    assert(cplus_transfer_ownership(&moved, &p) == 0);
    assert(p == NULL && moved != NULL);
    assert(cplus_alloc_check_generation(moved, gen_before) == 0);
    assert(cplus_alloc_get_meta(moved, &meta) == 0 && meta.generation > gen_before);
    assert(cplus_transfer_ownership(&moved, &moved) != 0);

    /* 3. cplus_realloc grow, shrink, and OOM preservation */
    memset(moved, 0x5A, 32);
    cplus_alloc_fail_after(0);
    void *fail_r = cplus_realloc(moved, 128);
    assert(fail_r == NULL);
    assert(cplus_alloc_is_live(moved) == 1 && cplus_alloc_size(moved) == 32);
    cplus_alloc_fail_reset();

    void *grown = cplus_realloc(moved, 128);
    assert(grown != NULL && cplus_alloc_is_live(grown) == 1 && cplus_alloc_size(grown) == 128);
    assert(((unsigned char *)grown)[0] == 0x5A && ((unsigned char *)grown)[31] == 0x5A);
    moved = grown;

    void *shrunk = cplus_realloc(moved, 16);
    assert(shrunk != NULL && cplus_alloc_size(shrunk) == 16);
    assert(((unsigned char *)shrunk)[0] == 0x5A && ((unsigned char *)shrunk)[15] == 0x5A);

    /* 4. cplus_free_checked + double-free + invalid-free tracking */
    unsigned long df_before = cplus_alloc_double_free_count();
    unsigned long inv_before = cplus_alloc_invalid_free_count();
    void *alias = shrunk;
    assert(cplus_free_checked(&shrunk) == 0 && shrunk == NULL);
    assert(cplus_alloc_is_live(alias) == 0);
    assert(cplus_alloc_size(alias) == 0);

    /* Double free caught without crashing or freeing twice */
    assert(cplus_free_checked(&alias) != 0 && alias == NULL);
    assert(cplus_alloc_double_free_count() == df_before + 1);

    /* Invalid stack pointer free refused */
    int stack_obj = 7;
    void *bad = &stack_obj;
    assert(cplus_free_checked(&bad) != 0 && bad == NULL);
    assert(cplus_alloc_invalid_free_count() == inv_before + 1);

    /* 5. cplus_calloc element size + overflow */
    void *arr = cplus_calloc(6, sizeof(double));
    assert(arr != NULL);
    assert(cplus_alloc_elem_size(arr) == sizeof(double));
    assert(cplus_alloc_length(arr) == 6);
    assert(cplus_alloc_size(arr) == 6 * sizeof(double));
    cplus_free(arr);

    errno = 0;
    assert(cplus_calloc(SIZE_MAX, 4) == NULL);

    /* 6. zero size and cplus_strdup / cplus_strndup metadata */
    void *z = cplus_malloc(0);
    assert(z != NULL && cplus_alloc_is_live(z) == 1);
    cplus_free(z);

    char *d1 = cplus_strdup("hello");
    assert(d1 != NULL && strcmp(d1, "hello") == 0 && cplus_alloc_is_live(d1));
    char *d2 = cplus_strndup("abcdef", 3);
    assert(d2 != NULL && strcmp(d2, "abc") == 0 && cplus_alloc_is_live(d2));
    cplus_free(d1);
    cplus_free(d2);

    assert(cplus_alloc_live_count() == base_live);
    assert(cplus_alloc_live_bytes() == base_bytes);

    puts("test_alloc_metadata: ok");
    return 0;
}
