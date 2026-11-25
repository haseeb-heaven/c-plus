/* Exact, minimal reproducers for every genuine finding in FINDINGS.md.
 *
 *   ./build/c+ build tests/redblue20/red_alloc/repro.cp \
 *     --default --sanitize=none -I. \
 *     -o tests/redblue20/red_alloc/out/repro \
 *     -- build/libc/libcplus_hardened.a
 *   tests/redblue20/red_alloc/out/repro <1..11>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/mempool.hp"
#include "stdlib/datastack.hp"

int main(int argc, char **argv) {
    error e = error();
    int which = argc > 1 ? atoi(argv[1]) : 0;
    stdlib_set_error_callback((stdlib_error_callback)0);

    if (which == 1) { /* F-ALIGN-01: default alloc ignores align */
        int i, misaligned = 0;
        for (i = 0; i < 32; ++i) {
            void *p = stdlib_allocate((const stdlib_allocator *)0, 48, 4096, &e);
            if (p == (void *)0) continue;
            if (((uintptr_t)p & 4095u) != 0) ++misaligned;
            stdlib_release(p);
        }
        printf("F-ALIGN-01 alloc misaligned=%d/32\n", misaligned);
        return misaligned > 0 ? 0 : 1;
    }
    if (which == 2) { /* F-ALIGN-01b: default realloc ignores align */
        int i, misaligned = 0;
        for (i = 0; i < 32; ++i) {
            void *p = stdlib_allocate((const stdlib_allocator *)0, 48, 16, &e);
            void *q;
            if (p == (void *)0) continue;
            q = stdlib_reallocate((const stdlib_allocator *)0, p, 48, 64, 4096, &e);
            if (q == (void *)0) continue;
            if (((uintptr_t)q & 4095u) != 0) ++misaligned;
            stdlib_release(q);
        }
        printf("F-ALIGN-01b realloc misaligned=%d/32\n", misaligned);
        return misaligned > 0 ? 0 : 1;
    }
    if (which == 3) { /* F-ARENA-02a: forged huge old_size -> memcpy crash */
        stdlib_arena a;
        unsigned char *p;
        stdlib_arena_init(&a, 512, &e);
        p = (unsigned char *)stdlib_arena_alloc(&a, 64, 16, &e);
        printf("F-ARENA-02a before realloc p=%p old_size=2^50\n", (void *)p);
        fflush(stdout);
        (void)stdlib_arena_realloc(&a, p, (size_t)1 << 50, 32, 16, &e);
        printf("F-ARENA-02a SURVIVED\n");
        return 1;
    }
    if (which == 4) { /* F-ARENA-02b: forged old_size aliases top -> canary */
        stdlib_arena a;
        unsigned char *p;
        unsigned char *q;
        unsigned char *n;
        int i, changed = 0;
        stdlib_arena_init(&a, 512, &e);
        p = (unsigned char *)stdlib_arena_alloc(&a, 64, 16, &e);
        q = (unsigned char *)stdlib_arena_alloc(&a, 64, 16, &e);
        for (i = 0; i < 16; ++i) q[i] = 0xA5;
        (void)stdlib_arena_realloc(&a, p, (size_t)(q - p) + 64u, 16, 16, &e);
        n = (unsigned char *)stdlib_arena_alloc(&a, 64, 16, &e);
        for (i = 0; i < 16; ++i) if (q[i] != 0xA5) ++changed;
        printf("F-ARENA-02b q_canary_destroyed=%d/16 fresh=%p\n", changed, (void *)n);
        return changed > 0 ? 0 : 1;
    }
    if (which == 5) { /* F-POOL-01: slab raw-size overflow */
        stdlib_mem_pool p;
        stdlib_mem_pool_init(&p, 1, (size_t)0x78787878787878cULL, &e);
        printf("F-POOL-01 init ok slots_per_slab=%zu; now alloc\n", p.slots_per_slab);
        fflush(stdout);
        (void)stdlib_mem_pool_alloc(&p, &e);
        printf("F-POOL-01 SURVIVED\n");
        return 1;
    }
    if (which == 6) { /* F-POOL-02: slot_pool_init product overflow */
        stdlib_slot_pool sp;
        stdlib_slot_ref r;
        int rc = stdlib_slot_pool_init(&sp, ((size_t)1 << 61) + 1u, 0, &e);
        printf("F-POOL-02 init rc=%d; now alloc_ref\n", rc);
        fflush(stdout);
        (void)stdlib_slot_pool_alloc_ref(&sp, &r, &e);
        printf("F-POOL-02 SURVIVED\n");
        return 1;
    }
    if (which == 7) { /* F-POOL-03: wild foreign pointer free.
                       * Precondition: pool already owns >=1 slab, otherwise
                       * the ownership loop body never reads the header. */
        stdlib_mem_pool p;
        stdlib_mem_pool_init(&p, 64, 4, &e);
        (void)stdlib_mem_pool_alloc(&p, &e);
        printf("F-POOL-03 release wild=(void*)0x1000\n");
        fflush(stdout);
        stdlib_mem_pool_release(&p, (void *)(uintptr_t)0x1000u, &e);
        printf("F-POOL-03 SURVIVED failed=%d code=%d\n", e.failed_now(), e.code());
        return 1;
    }
    if (which == 8) { /* F-DS-01: forged old_size -> memcpy crash */
        stdlib_datastack ds;
        unsigned char *p;
        stdlib_datastack_init(&ds, 16384, &e);
        p = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 16, &e);
        printf("F-DS-01 before realloc p=%p old_size=2^40\n", (void *)p);
        fflush(stdout);
        (void)stdlib_datastack_realloc(&ds, p, (size_t)1 << 40, 32, 16, &e);
        printf("F-DS-01 SURVIVED\n");
        return 1;
    }
    if (which == 9) { /* F-DS-02: huge heap capacity aborts */
        stdlib_datastack ds;
        printf("F-DS-02 init(capacity=SIZE_MAX)\n");
        fflush(stdout);
        {
            int rc = stdlib_datastack_init(&ds, SIZE_MAX, &e);
            printf("F-DS-02 SURVIVED rc=%d failed=%d code=%d\n",
                   rc, e.failed_now(), e.code());
        }
        return 1;
    }
    if (which == 10) { /* F-DS-03: out-of-order scratch leave */
        stdlib_datastack *ds = stdlib_thread_datastack(&e);
        stdlib_scratch_mark m1, m2;
        unsigned char *p1;
        unsigned char *p2;
        unsigned char *n;
        int i, changed = 0;
        m1 = stdlib_scratch_enter(ds);
        p1 = (unsigned char *)stdlib_datastack_alloc(ds, 64, 16, &e);
        for (i = 0; i < 16; ++i) p1[i] = 0x91;
        m2 = stdlib_scratch_enter(ds);
        p2 = (unsigned char *)stdlib_datastack_alloc(ds, 64, 16, &e);
        for (i = 0; i < 16; ++i) p2[i] = 0x82;
        stdlib_scratch_leave(ds, m2); /* inner leave first: out of order */
        stdlib_scratch_leave(ds, m1); /* outer leave rewinds over live p1 */
        n = (unsigned char *)stdlib_datastack_alloc(ds, 32, 16, &e);
        for (i = 0; i < 16; ++i) if (p1[i] != 0x91) ++changed;
        printf("F-DS-03 p1_canary_destroyed=%d/16 fresh=%p\n", changed, (void *)n);
        return changed > 0 ? 0 : 1;
    }
    if (which == 11) { /* F-DS-04: forged release size */
        stdlib_datastack ds;
        unsigned char *p1;
        unsigned char *p2;
        unsigned char *n;
        int i, changed = 0;
        stdlib_datastack_init(&ds, 8192, &e);
        p1 = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 16, &e);
        p2 = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 16, &e);
        for (i = 0; i < 16; ++i) p1[i] = 0xE7;
        (void)p2;
        stdlib_datastack_release(&ds, p1, 128, &e); /* real size is 64 */
        n = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 16, &e);
        for (i = 0; i < 16; ++i) if (p1[i] != 0xE7) ++changed;
        printf("F-DS-04 p1_canary_destroyed=%d/16 fresh=%p\n", changed, (void *)n);
        return changed > 0 ? 0 : 1;
    }
    printf("usage: repro <1..11>\n");
    return 2;
}
