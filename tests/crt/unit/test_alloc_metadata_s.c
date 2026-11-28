/* Unit test: SafeCRT allocation metadata, length query, ownership transfer,
 * generation ABA protection, failure injection, aligned_alloc_s, and
 * double/invalid free detection using standard C signatures. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_alloc_meta meta;
    size_t base_live = cplus_safe_alloc_live_count();
    size_t base_bytes = cplus_safe_alloc_live_bytes();
    size_t cnt = 0;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* 1. malloc_s + metadata + cplus_safe_len */
    int *arr = (int *)malloc_s(5 * sizeof(int));
    assert(arr != NULL);
    assert(cplus_safe_ptr_is_live(arr) == 1);
    assert(cplus_safe_ptr_size(arr) == 5 * sizeof(int));
    assert(cplus_safe_len(arr) == 5);
    assert(cplus_safe_ptr_elem_size(arr) == 1);
    assert(cplus_safe_query_length(arr, sizeof(int), &cnt) == CPLUS_SAFE_OK && cnt == 5);
    assert(cplus_safe_alloc_get_meta(arr, &meta) == CPLUS_SAFE_OK);
    assert(meta.is_live == 1 && meta.byte_size == 5 * sizeof(int));
    assert(meta.generation > 0);
    assert(cplus_safe_ptr_check_generation(arr, meta.generation) == 1);
    assert(cplus_safe_ptr_check_generation(arr, meta.generation + 1) == 0);
    assert(cplus_safe_alloc_live_count() == base_live + 1);
    assert(cplus_safe_alloc_live_bytes() == base_bytes + 5 * sizeof(int));

    /* Interior pointer must NOT look live */
    assert(cplus_safe_ptr_is_live(arr + 1) == 0);
    assert(cplus_safe_ptr_size(arr + 1) == 0);
    assert(cplus_safe_query_length(arr + 1, sizeof(int), &cnt) == CPLUS_SAFE_EINVAL);

    /* 2. Ownership transfer bumps generation */
    void *moved = NULL;
    void *src = arr;
    unsigned long long old_gen = meta.generation;
    assert(cplus_safe_transfer_ownership(&moved, &src) == CPLUS_SAFE_OK);
    assert(src == NULL && moved == arr);
    assert(cplus_safe_ptr_check_generation(moved, old_gen) == 0);
    assert(cplus_safe_transfer_ownership(&moved, &moved) == CPLUS_SAFE_EINVAL);

    /* 3. calloc_s tracks element size & element count */
    double *dbls = (double *)calloc_s(4, sizeof(double));
    assert(dbls != NULL);
    assert(cplus_safe_ptr_elem_size(dbls) == sizeof(double));
    assert(cplus_safe_ptr_count(dbls) == 4);
    assert(cplus_safe_ptr_size(dbls) == 4 * sizeof(double));
    assert(cplus_safe_len(dbls) == 4);
    free_s(dbls);

    /* 4. aligned_alloc_s validates alignment & size */
    errno = 0;
    assert(aligned_alloc_s(3, 64) == NULL && errno == EINVAL);
    errno = 0;
    assert(aligned_alloc_s(2, 64) == NULL && errno == EINVAL);
    void *ap = aligned_alloc_s(64, 128);
    assert(ap != NULL && (((uintptr_t)ap) & 63u) == 0);
    assert(cplus_safe_ptr_is_live(ap) == 1 && cplus_safe_ptr_size(ap) == 128);
    free_s(ap);

    /* 5. Failure injection leaves existing block untouched */
    memset(moved, 0x33, 5 * sizeof(int));
    cplus_safe_alloc_fail_reset();
    unsigned long seq0 = cplus_safe_alloc_seq();
    cplus_safe_alloc_fail_after(1);
    void *ok1 = malloc_s(16);
    assert(ok1 != NULL);
    assert(cplus_safe_alloc_seq() == seq0 + 1);
    errno = 0;
    void *fail1 = malloc_s(16);
    assert(fail1 == NULL && errno == ENOMEM);
    errno = 0;
    assert(realloc_s(moved, 128) == NULL && errno == ENOMEM);
    assert(cplus_safe_ptr_is_live(moved) == 1 && cplus_safe_ptr_size(moved) == 5 * sizeof(int));
    cplus_safe_alloc_fail_reset();
    free_s(ok1);

    /* 6. realloc preserves element size and updates byte size */
    void *grown = realloc_s(moved, 10 * sizeof(int));
    assert(grown != NULL && cplus_safe_ptr_size(grown) == 10 * sizeof(int));
    assert(((unsigned char *)grown)[0] == 0x33);

    /* 7. free_checked_s rejects double-free, stack pointer, raw malloc */
    assert(free_checked_s(grown) == CPLUS_SAFE_OK);
    assert(cplus_safe_ptr_is_live(grown) == 0);
    errno = 0;
    assert(free_checked_s(grown) == CPLUS_SAFE_EINVAL && errno == EINVAL);

    int stack_val = 42;
    errno = 0;
    assert(free_checked_s(&stack_val) == CPLUS_SAFE_EINVAL && errno == EINVAL);

    void *raw = malloc(24);
    assert(raw != NULL);
    errno = 0;
    assert(free_checked_s(raw) == CPLUS_SAFE_EINVAL && errno == EINVAL);
    assert(realloc_s(raw, 48) == NULL);
    free(raw);

    assert(cplus_safe_alloc_live_count() == base_live);
    assert(cplus_safe_alloc_live_bytes() == base_bytes);

    puts("unit alloc_metadata_s: ok");
    return 0;
}
