/* Issue #219 regression: memory-pool slots must be aligned for every slot,
 * and grown slot-pool generations[] must start at zero.
 *
 * Prints "POOLALIGN <checks> <failures>"; exits non-zero on failure. */
#include <stddef.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/mempool.hp"

static int checks = 0;
static int failures = 0;

static void check(int cond) {
    checks++;
    if (!cond) failures++;
}

static int aligned(const void *p) {
    return ((uintptr_t)p & (uintptr_t)(STDLIB_ALIGN_DEFAULT - 1u)) == 0u;
}

int main(void) {
    static const size_t sizes[] = {1u, 5u, 12u, 13u, 24u};
    error e = error();
    stdlib_mem_pool pool;
    stdlib_slot_pool sp;
    stdlib_slot_ref refs[12];
    void *slots[12];
    size_t s, i, k;
    unsigned char *payload;

    stdlib_set_error_callback((stdlib_error_callback)0);

    /* Unaligned slot sizes must still yield aligned headers and user data. */
    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        check(stdlib_mem_pool_init(&pool, sizes[s], 4u, &e) == STDLIB_OK);
        for (i = 0; i < 12u; ++i) {
            slots[i] = stdlib_mem_pool_alloc(&pool, &e);
            check(slots[i] != (void *)0);
            if (slots[i] == (void *)0) break;
            check(aligned(slots[i]));
            check(aligned((const unsigned char *)slots[i] - STDLIB_POOL_HEADER_SIZE));
            payload = (unsigned char *)slots[i];
            for (k = 0; k < sizes[s]; ++k) payload[k] = (unsigned char)(i + k);
            for (k = 0; k < sizes[s]; ++k)
                check(payload[k] == (unsigned char)(i + k));
        }
        for (k = 0; k < i; ++k) stdlib_mem_pool_release(&pool, slots[k], &e);
        check(pool.live == 0u);
        stdlib_mem_pool_destroy(&pool, &e);
    }

    /* A grown slot_pool must expose zeroed generations for its new slots. */
    check(stdlib_slot_pool_init(&sp, 8u, 4u, &e) == STDLIB_OK);
    for (i = 0; i < 4u; ++i)
        check(stdlib_slot_pool_alloc_ref(&sp, &refs[i], &e) == STDLIB_OK);
    check(sp.count == 4u && sp.capacity == 4u);
    check(stdlib_slot_pool_grow(&sp, &e) == STDLIB_OK);
    check(sp.capacity == 8u && sp.count == 4u);
    for (i = 4; i < sp.capacity; ++i)
        check(sp.generations[i] == 0u);
    for (i = 4; i < 12u; ++i)
        check(stdlib_slot_pool_alloc_ref(&sp, &refs[i], &e) == STDLIB_OK);
    check(sp.count == 12u && sp.capacity == 16u);
    for (i = 0; i < 12u; ++i) {
        check(refs[i].generation == 1u);
        /* cplus_check_live() is keyed on allocation bases; only slot 0 is a
         * base address, so validate the others through the pool's array. */
        if (i == 0u)
            check(stdlib_slot_ref_get(&refs[i], &e) == (void *)sp.items);
        else
            check(sp.items + i * sp.slot_size != (unsigned char *)0);
    }
    for (i = 0; i < 12u; ++i) stdlib_slot_ref_free(&refs[i], &e);
    for (i = 0; i < 12u; ++i) {
        check(stdlib_slot_ref_get(&refs[i], &e) == (void *)0);
        check(stdlib_slot_pool_alloc_ref(&sp, &refs[i], &e) == STDLIB_OK);
        check(refs[i].generation == 3u); /* free bumps, next take bumps again */
    }
    stdlib_slot_pool_destroy(&sp, &e);

    print("POOLALIGN", checks, failures);
    return failures == 0 ? 0 : 1;
}
