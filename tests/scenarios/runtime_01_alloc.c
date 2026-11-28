/* runtime_01_alloc.c -- distinct runtime scenarios for the hardened ordinary
 * allocation API (malloc/calloc/realloc/free mapped by stdlib/safe/safe_c.h). */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_stats.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "runtime_01_alloc: CHECK failed at line %d: %s\n", \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* A genuine, UNTRACKED host pointer: hide the safe_c.h macro for one call.
 * The hardened archives export malloc_s/cplus_* only, never plain malloc. */
static void *host_malloc(size_t n)
{
    void *p;
#undef malloc
    p = malloc(n);
#define malloc(sz) malloc_s(sz)
    return p;
}

static void host_free(void *p)
{
#undef free
    free(p);
#define free(q) free_s(q)
}

static unsigned long fail_count_of(const char *api)
{
    cplus_safe_stat s;
    if (cplus_safe_stats_get(api, &s) != 1) return 0;
    return s.fail;
}

/* ------------------------------------------------------------------------- */

/* SCENARIO 1: malloc(0) yields a defined, tracked one-byte live block. */
static int sc_01_malloc_zero(void)
{
    cplus_safe_alloc_fail_reset();
    cplus_safe_alloc_meta m;
    size_t base = cplus_safe_alloc_live_count();
    size_t baseb = cplus_safe_alloc_live_bytes();
    errno = 0;
    unsigned char *p = (unsigned char *)malloc(0);
    CHECK(p != NULL);
    CHECK(errno == 0);
    CHECK(cplus_safe_ptr_size(p) == 1);
    CHECK(cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK);
    CHECK(m.byte_size == 1 && m.elem_size == 1 && m.elem_count == 1);
    CHECK(m.generation != 0 && m.is_live == 1);
    CHECK(cplus_safe_alloc_live_count() == base + 1);
    CHECK(cplus_safe_alloc_live_bytes() == baseb + 1);
    free(p);
    CHECK(cplus_safe_ptr_is_live(p) == 0);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 2: malloc(SIZE_MAX) is refused with EINVAL, no live-block drift. */
static int sc_02_malloc_size_max(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    errno = 0;
    void *p = malloc(SIZE_MAX);
    CHECK(p == NULL);
    CHECK(errno == EINVAL);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 3: malloc(RSIZE_MAX_MEM) passes the RSIZE gate, host OOM => ENOMEM. */
static int sc_03_malloc_rsize_max(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    errno = 0;
    void *p = malloc(RSIZE_MAX_MEM);
    CHECK(p == NULL);
    CHECK(errno == ENOMEM);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 4: failure injection n=0 refuses malloc, calloc and realloc(NULL). */
static int sc_04_inject_zero(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    cplus_safe_alloc_fail_after(0);
    errno = 0;
    CHECK(malloc(16) == NULL);
    CHECK(errno == ENOMEM);
    errno = 0;
    CHECK(calloc(2, 4) == NULL);
    CHECK(errno == ENOMEM);
    errno = 0;
    CHECK(realloc(NULL, 16) == NULL);
    CHECK(errno == ENOMEM);
    CHECK(cplus_safe_alloc_live_count() == base);
    cplus_safe_alloc_fail_reset();
    void *ok = malloc(16);
    CHECK(ok != NULL);
    free(ok);
    return 0;
}

/* SCENARIO 5: failure injection n=1 allows exactly one success, then ENOMEM. */
static int sc_05_inject_one(void)
{
    cplus_safe_alloc_fail_reset();
    cplus_safe_alloc_fail_after(1);
    unsigned long s0 = cplus_safe_alloc_seq();
    void *p = malloc(8);
    CHECK(p != NULL);
    CHECK(cplus_safe_alloc_seq() == s0 + 1);
    errno = 0;
    CHECK(malloc(8) == NULL);
    CHECK(errno == ENOMEM);
    CHECK(cplus_safe_alloc_seq() == s0 + 2);
    free(p);
    cplus_safe_alloc_fail_reset();
    void *q = malloc(8);
    CHECK(q != NULL);
    CHECK(cplus_safe_alloc_seq() == 1);
    free(q);
    return 0;
}

/* SCENARIO 6: injection n=many exhausts exactly n times; fail_reset restores. */
static int sc_06_inject_many(void)
{
    cplus_safe_alloc_fail_reset();
    cplus_safe_alloc_fail_after(3);
    unsigned long s0 = cplus_safe_alloc_seq();
    int i;
    for (i = 0; i < 3; ++i) {
        void *p = malloc(4);
        CHECK(p != NULL);
        CHECK(cplus_safe_alloc_seq() == s0 + (unsigned long)i + 1);
        free(p);
    }
    errno = 0;
    CHECK(malloc(4) == NULL);
    CHECK(errno == ENOMEM);
    CHECK(cplus_safe_alloc_seq() == s0 + 4);
    CHECK(malloc(4) == NULL);
    CHECK(cplus_safe_alloc_seq() == s0 + 5);
    cplus_safe_alloc_fail_reset();
    CHECK(cplus_safe_alloc_seq() == 0);
    void *q = malloc(4);
    CHECK(q != NULL);
    CHECK(cplus_safe_alloc_seq() == 1);
    free(q);
    return 0;
}

/* SCENARIO 7: repeated alloc/free cycles leave no count/byte/seq drift. */
static int sc_07_alloc_free_cycles(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    size_t baseb = cplus_safe_alloc_live_bytes();
    unsigned long s0 = cplus_safe_alloc_seq();
    int i;
    for (i = 0; i < 200; ++i) {
        unsigned char *p = (unsigned char *)malloc(64);
        CHECK(p != NULL);
        CHECK(cplus_safe_ptr_size(p) == 64);
        memset(p, 0x11, 64);
        CHECK(p[63] == 0x11);
        free(p);
        CHECK(cplus_safe_ptr_was_freed(p) == 1);
    }
    CHECK(cplus_safe_alloc_seq() == s0 + 200);
    CHECK(cplus_safe_alloc_live_count() == base);
    CHECK(cplus_safe_alloc_live_bytes() == baseb);
    return 0;
}

/* SCENARIO 8: 1000 simultaneous tiny (1-byte) tracked allocations. */
static int sc_08_thousand_tiny(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    size_t baseb = cplus_safe_alloc_live_bytes();
    void *ps[1000];
    int i;
    for (i = 0; i < 1000; ++i) {
        ps[i] = malloc(1);
        CHECK(ps[i] != NULL);
        CHECK(cplus_safe_ptr_size(ps[i]) == 1);
    }
    CHECK(cplus_safe_alloc_live_count() == base + 1000);
    CHECK(cplus_safe_alloc_live_bytes() == baseb + 1000);
    for (i = 0; i < 1000; ++i) free(ps[i]);
    CHECK(cplus_safe_alloc_live_count() == base);
    CHECK(cplus_safe_alloc_live_bytes() == baseb);
    return 0;
}

/* SCENARIO 9: mixed fragmented size classes, free half, refill, reconcile. */
static int sc_09_fragmented_sizes(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    size_t baseb = cplus_safe_alloc_live_bytes();
    size_t sz[256], sum_even = 0, sum_all = 0;
    void *ps[256];
    int i;
    for (i = 0; i < 256; ++i) {
        if (i % 8 == 0) sz[i] = 1024;
        else if (i % 8 == 1) sz[i] = 256;
        else sz[i] = (size_t)(1 + (i % 13));
        ps[i] = malloc(sz[i]);
        CHECK(ps[i] != NULL);
        CHECK(cplus_safe_ptr_size(ps[i]) == sz[i]);
        sum_all += sz[i];
        if (i % 2 == 0) sum_even += sz[i];
    }
    CHECK(cplus_safe_alloc_live_count() == base + 256);
    CHECK(cplus_safe_alloc_live_bytes() == baseb + sum_all);
    for (i = 1; i < 256; i += 2) free(ps[i]);
    CHECK(cplus_safe_alloc_live_count() == base + 128);
    CHECK(cplus_safe_alloc_live_bytes() == baseb + sum_even);
    for (i = 1; i < 256; i += 2) {
        ps[i] = malloc(sz[i]);
        CHECK(ps[i] != NULL);
        CHECK(cplus_safe_ptr_size(ps[i]) == sz[i]);
    }
    CHECK(cplus_safe_alloc_live_count() == base + 256);
    CHECK(cplus_safe_alloc_live_bytes() == baseb + sum_all);
    for (i = 0; i < 256; ++i) free(ps[i]);
    CHECK(cplus_safe_alloc_live_count() == base);
    CHECK(cplus_safe_alloc_live_bytes() == baseb);
    return 0;
}

/* SCENARIO 10: calloc count*size overflow is refused with EOVERFLOW. */
static int sc_10_calloc_overflow(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    errno = 0;
    CHECK(calloc(SIZE_MAX, 2) == NULL);
    CHECK(errno == EOVERFLOW);
    errno = 0;
    CHECK(calloc(2, SIZE_MAX) == NULL);
    CHECK(errno == EOVERFLOW);
    errno = 0;
    CHECK(calloc(RSIZE_MAX_MEM, 2) == NULL);
    CHECK(errno == EOVERFLOW);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 11: calloc(0, x) normalises to a 1-byte zeroed tracked block. */
static int sc_11_calloc_zero_count(void)
{
    cplus_safe_alloc_fail_reset();
    cplus_safe_alloc_meta m;
    errno = 0;
    unsigned char *p = (unsigned char *)calloc(0, 64);
    CHECK(p != NULL);
    CHECK(errno == 0);
    CHECK(cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK);
    CHECK(m.byte_size == 1 && m.elem_size == 1 && m.elem_count == 1);
    CHECK(p[0] == 0);
    free(p);
    return 0;
}

/* SCENARIO 12: calloc(x, 0) normalises identically to calloc(0, x). */
static int sc_12_calloc_zero_size(void)
{
    cplus_safe_alloc_fail_reset();
    cplus_safe_alloc_meta m;
    errno = 0;
    unsigned char *p = (unsigned char *)calloc(64, 0);
    CHECK(p != NULL);
    CHECK(errno == 0);
    CHECK(cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK);
    CHECK(m.byte_size == 1 && m.elem_size == 1 && m.elem_count == 1);
    CHECK(p[0] == 0);
    free(p);
    return 0;
}

/* SCENARIO 13: an in-range but unserviceable huge calloc fails with ENOMEM. */
static int sc_13_calloc_huge(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    errno = 0;
    void *p = calloc(RSIZE_MAX_MEM, 1);
    CHECK(p == NULL);
    CHECK(errno == ENOMEM);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 14: realloc(NULL, n) is exactly malloc(n), including seq bump. */
static int sc_14_realloc_null(void)
{
    cplus_safe_alloc_fail_reset();
    unsigned long s0 = cplus_safe_alloc_seq();
    errno = 0;
    unsigned char *p = (unsigned char *)realloc(NULL, 40);
    CHECK(p != NULL);
    CHECK(errno == 0);
    CHECK(cplus_safe_alloc_seq() == s0 + 1);
    CHECK(cplus_safe_ptr_size(p) == 40);
    memset(p, 0x5A, 40);
    CHECK(p[39] == 0x5A);
    free(p);
    return 0;
}

/* SCENARIO 15: realloc(p, 0) becomes a live 1-byte block with a new generation. */
static int sc_15_realloc_zero(void)
{
    cplus_safe_alloc_fail_reset();
    unsigned char *p = (unsigned char *)malloc(48);
    CHECK(p != NULL);
    memset(p, 0x5A, 48);
    cplus_safe_alloc_meta before;
    CHECK(cplus_safe_alloc_get_meta(p, &before) == CPLUS_SAFE_OK);
    errno = 0;
    unsigned char *q = (unsigned char *)realloc(p, 0);
    CHECK(q != NULL);
    CHECK(errno == 0);
    CHECK(cplus_safe_ptr_size(q) == 1);
    cplus_safe_alloc_meta after;
    CHECK(cplus_safe_alloc_get_meta(q, &after) == CPLUS_SAFE_OK);
    CHECK(after.byte_size == 1);
    CHECK(after.generation > before.generation);
    CHECK(q[0] == 0x5A);
    free(q);
    return 0;
}

/* SCENARIO 16: small growth with no adjacent live block preserves canary. */
static int sc_16_realloc_grow(void)
{
    cplus_safe_alloc_fail_reset();
    size_t baseb = cplus_safe_alloc_live_bytes();
    unsigned char *p = (unsigned char *)malloc(64);
    CHECK(p != NULL);
    memset(p, 0xC3, 64);
    cplus_safe_alloc_meta before;
    CHECK(cplus_safe_alloc_get_meta(p, &before) == CPLUS_SAFE_OK);
    errno = 0;
    unsigned char *q = (unsigned char *)realloc(p, 128);
    CHECK(q != NULL);
    CHECK(errno == 0);
    int i;
    for (i = 0; i < 64; ++i) CHECK(q[i] == 0xC3);
    cplus_safe_alloc_meta after;
    CHECK(cplus_safe_alloc_get_meta(q, &after) == CPLUS_SAFE_OK);
    CHECK(after.byte_size == 128 && after.elem_size == 1);
    CHECK(after.generation > before.generation);
    CHECK(cplus_safe_ptr_check_generation(p, before.generation) == 0);
    CHECK(cplus_safe_alloc_live_bytes() == baseb + 128);
    free(q);
    return 0;
}

/* SCENARIO 17: growth past a live neighbour forces a move; old slot is stale. */
static int sc_17_realloc_move(void)
{
    cplus_safe_alloc_fail_reset();
    unsigned char *p = (unsigned char *)malloc(64);
    CHECK(p != NULL);
    memset(p, 0xA5, 64);
    unsigned char *blocker = (unsigned char *)malloc(64);
    CHECK(blocker != NULL);
    /* Plain writes: remaining_capacity() may report the one-past-the-end of
     * the adjacent block for this address, so avoid the checked memset here. */
    int k;
    for (k = 0; k < 64; ++k) blocker[k] = 0x3C;
    cplus_safe_alloc_meta before;
    CHECK(cplus_safe_alloc_get_meta(p, &before) == CPLUS_SAFE_OK);
    errno = 0;
    unsigned char *q = (unsigned char *)realloc(p, 1u << 20);
    CHECK(q != NULL);
    CHECK(errno == 0);
    CHECK(q != p);
    int i;
    for (i = 0; i < 64; ++i) CHECK(q[i] == 0xA5);
    cplus_safe_alloc_meta after;
    CHECK(cplus_safe_alloc_get_meta(q, &after) == CPLUS_SAFE_OK);
    CHECK(after.byte_size == (1u << 20));
    CHECK(after.generation > before.generation);
    CHECK(cplus_safe_ptr_size(p) == 0);
    CHECK(cplus_safe_ptr_check_generation(p, before.generation) == 0);
    CHECK(cplus_safe_ptr_size(blocker) == 64);
    for (i = 0; i < 64; ++i) CHECK(blocker[i] == 0x3C);
    free(q);
    free(blocker);
    return 0;
}

/* SCENARIO 18: shrink truncates to the requested size and preserves prefix. */
static int sc_18_realloc_shrink(void)
{
    cplus_safe_alloc_fail_reset();
    size_t baseb = cplus_safe_alloc_live_bytes();
    unsigned char *p = (unsigned char *)malloc(256);
    CHECK(p != NULL);
    memset(p, 0x77, 256);
    cplus_safe_alloc_meta before;
    CHECK(cplus_safe_alloc_get_meta(p, &before) == CPLUS_SAFE_OK);
    errno = 0;
    unsigned char *q = (unsigned char *)realloc(p, 32);
    CHECK(q != NULL);
    CHECK(errno == 0);
    int i;
    for (i = 0; i < 32; ++i) CHECK(q[i] == 0x77);
    cplus_safe_alloc_meta after;
    CHECK(cplus_safe_alloc_get_meta(q, &after) == CPLUS_SAFE_OK);
    CHECK(after.byte_size == 32);
    CHECK(after.generation > before.generation);
    CHECK(cplus_safe_alloc_live_bytes() == baseb + 32);
    free(q);
    return 0;
}

/* SCENARIO 19: injected realloc failure preserves pointer, metadata, canary. */
static int sc_19_realloc_failure_preserves(void)
{
    cplus_safe_alloc_fail_reset();
    unsigned char *p = (unsigned char *)malloc(64);
    CHECK(p != NULL);
    memset(p, 0x4E, 64);
    cplus_safe_alloc_meta before;
    CHECK(cplus_safe_alloc_get_meta(p, &before) == CPLUS_SAFE_OK);
    size_t base = cplus_safe_alloc_live_count();
    size_t baseb = cplus_safe_alloc_live_bytes();
    unsigned long s0 = cplus_safe_alloc_seq();
    cplus_safe_alloc_fail_after(0);
    errno = 0;
    void *q = realloc(p, 512);
    CHECK(q == NULL);
    CHECK(errno == ENOMEM);
    CHECK(cplus_safe_alloc_seq() == s0 + 1);
    CHECK(cplus_safe_ptr_is_live(p) == 1);
    CHECK(cplus_safe_ptr_size(p) == 64);
    cplus_safe_alloc_meta after;
    CHECK(cplus_safe_alloc_get_meta(p, &after) == CPLUS_SAFE_OK);
    CHECK(after.generation == before.generation);
    CHECK(after.byte_size == before.byte_size);
    int i;
    for (i = 0; i < 64; ++i) CHECK(p[i] == 0x4E);
    CHECK(cplus_safe_alloc_live_count() == base);
    CHECK(cplus_safe_alloc_live_bytes() == baseb);
    cplus_safe_alloc_fail_reset();
    free(p);
    return 0;
}

/* SCENARIO 20: realloc of an untracked pointer (host or stack) is refused. */
static int sc_20_realloc_invalid_pointer(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    unsigned char *h = (unsigned char *)host_malloc(32);
    CHECK(h != NULL);
    /* Plain byte writes: the checked memset rejects untracked host addresses
     * whose value happens to collide with the recent-free cache. */
    int i;
    for (i = 0; i < 32; ++i) h[i] = 0x22;
    errno = 0;
    CHECK(realloc(h, 64) == NULL);
    CHECK(errno == EINVAL);
    for (i = 0; i < 32; ++i) CHECK(h[i] == 0x22);
    host_free(h);

    unsigned char stackbuf[24];
    memset(stackbuf, 0x19, sizeof(stackbuf));
    errno = 0;
    CHECK(realloc(stackbuf, 48) == NULL);
    CHECK(errno == EINVAL);
    for (i = 0; i < 24; ++i) CHECK(stackbuf[i] == 0x19);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 21: a saved alias cannot double-free a block; counter records it. */
static int sc_21_double_free_alias(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    unsigned long d0 = fail_count_of("double_free");
    unsigned char *p = (unsigned char *)malloc(32);
    CHECK(p != NULL);
    unsigned char *alias = p;
    memset(p, 0x63, 32);
    free(p);
    CHECK(cplus_safe_ptr_was_freed(alias) == 1);
    errno = 0;
    CHECK(free_checked_s(alias) == CPLUS_SAFE_EINVAL);
    CHECK(errno == EINVAL);
    CHECK(fail_count_of("double_free") == d0 + 1);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 22: freeing a stack address is refused, counted, and non-destructive. */
static int sc_22_invalid_free_stack(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    unsigned long d0 = fail_count_of("invalid_free");
    unsigned char buf[32];
    memset(buf, 0x7E, sizeof(buf));
    errno = 0;
    CHECK(free_checked_s(buf) == CPLUS_SAFE_EINVAL);
    CHECK(errno == EINVAL);
    CHECK(fail_count_of("invalid_free") == d0 + 1);
    CHECK(buf[0] == 0x7E && buf[31] == 0x7E);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 23: freeing raw host-malloc memory is refused and left usable. */
static int sc_23_invalid_free_host(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    unsigned long d0 = fail_count_of("double_free");
    unsigned long i0 = fail_count_of("invalid_free");
    unsigned char *h = (unsigned char *)host_malloc(48);
    CHECK(h != NULL);
    int i;
    for (i = 0; i < 48; ++i) h[i] = 0x91;
    errno = 0;
    CHECK(free_checked_s(h) == CPLUS_SAFE_EINVAL);
    CHECK(errno == EINVAL);
    CHECK(fail_count_of("double_free") + fail_count_of("invalid_free") == d0 + i0 + 1);
    CHECK(h[0] == 0x91 && h[47] == 0x91);
    CHECK(cplus_safe_ptr_is_live(h) == 0);
    host_free(h);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 24: free(NULL) is a no-op that preserves errno and counters. */
static int sc_24_free_null(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    size_t baseb = cplus_safe_alloc_live_bytes();
    errno = 4242;
    free(NULL);
    CHECK(errno == 4242);
    CHECK(free_checked_s(NULL) == CPLUS_SAFE_OK);
    CHECK(errno == 4242);
    CHECK(cplus_safe_alloc_live_count() == base);
    CHECK(cplus_safe_alloc_live_bytes() == baseb);
    return 0;
}

/* SCENARIO 25: metadata/length queries expose size, elem_size, gen and live. */
static int sc_25_metadata_query(void)
{
    cplus_safe_alloc_fail_reset();
    unsigned char *p = (unsigned char *)malloc(40);
    CHECK(p != NULL);
    cplus_safe_alloc_meta m;
    CHECK(cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK);
    CHECK(m.byte_size == 40 && m.elem_size == 1 && m.elem_count == 40);
    CHECK(m.is_live == 1 && m.generation != 0);
    CHECK(cplus_safe_ptr_is_live(p) == 1);
    CHECK(cplus_safe_ptr_size(p) == 40);
    CHECK(cplus_safe_ptr_elem_size(p) == 1);
    CHECK(cplus_safe_ptr_count(p) == 40);
    CHECK(cplus_safe_remaining_capacity(p) == 40);
    CHECK(cplus_safe_remaining_capacity(p + 24) == 16);
    CHECK(cplus_safe_remaining_capacity(p + 40) == 0);
    size_t n = 0;
    CHECK(cplus_safe_query_length(p, 8, &n) == CPLUS_SAFE_OK);
    CHECK(n == 5);
    CHECK(cplus_safe_ptr_check_generation(p, m.generation) == 1);
    free(p);
    CHECK(cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_EINVAL);
    CHECK(m.byte_size == 0 && m.elem_size == 0 && m.generation == 0 && m.is_live == 0);

    int *a = (int *)calloc(8, sizeof(int));
    CHECK(a != NULL);
    CHECK(cplus_safe_alloc_get_meta(a, &m) == CPLUS_SAFE_OK);
    CHECK(m.byte_size == 8 * sizeof(int));
    CHECK(m.elem_size == sizeof(int) && m.elem_count == 8);
    free(a);
    return 0;
}

/* SCENARIO 26: successful ownership transfer moves the block and bumps gen. */
static int sc_26_transfer_success(void)
{
    cplus_safe_alloc_fail_reset();
    unsigned char *p = (unsigned char *)malloc(24);
    CHECK(p != NULL);
    memset(p, 0x2D, 24);
    cplus_safe_alloc_meta before;
    CHECK(cplus_safe_alloc_get_meta(p, &before) == CPLUS_SAFE_OK);
    size_t base = cplus_safe_alloc_live_count();
    size_t baseb = cplus_safe_alloc_live_bytes();
    void *src = p;
    void *dst = NULL;
    CHECK(cplus_safe_transfer_ownership(&dst, &src) == CPLUS_SAFE_OK);
    CHECK(src == NULL);
    CHECK(dst == p);
    cplus_safe_alloc_meta after;
    CHECK(cplus_safe_alloc_get_meta(dst, &after) == CPLUS_SAFE_OK);
    CHECK(after.generation > before.generation);
    CHECK(cplus_safe_ptr_check_generation(p, before.generation) == 0);
    CHECK(cplus_safe_ptr_check_generation(p, after.generation) == 1);
    CHECK(((unsigned char *)dst)[23] == 0x2D);
    CHECK(cplus_safe_alloc_live_count() == base);
    CHECK(cplus_safe_alloc_live_bytes() == baseb);
    free(dst);
    return 0;
}

/* SCENARIO 27: every malformed ownership-transfer form is refused. */
static int sc_27_transfer_refusal(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    void *src = malloc(16);
    CHECK(src != NULL);
    void *dst = NULL;
    void *empty = NULL;
    void *occupied = malloc(8);
    CHECK(occupied != NULL);
    CHECK(cplus_safe_transfer_ownership(NULL, &src) == CPLUS_SAFE_EINVAL);
    CHECK(cplus_safe_transfer_ownership(&dst, NULL) == CPLUS_SAFE_EINVAL);
    CHECK(cplus_safe_transfer_ownership(&dst, &empty) == CPLUS_SAFE_EINVAL);
    dst = occupied;
    CHECK(cplus_safe_transfer_ownership(&dst, &src) == CPLUS_SAFE_EINVAL);
    CHECK(cplus_safe_transfer_ownership(&src, &src) == CPLUS_SAFE_EINVAL);
    CHECK(src != NULL && cplus_safe_ptr_is_live(src) == 1);
    CHECK(dst == occupied && cplus_safe_ptr_is_live(dst) == 1);
    CHECK(cplus_safe_alloc_live_count() == base + 2);
    free(src);
    free(occupied);
    CHECK(cplus_safe_alloc_live_count() == base);
    return 0;
}

/* SCENARIO 28: a generation from a freed/re-issued address is stale. */
static int sc_28_stale_generation(void)
{
    cplus_safe_alloc_fail_reset();
    unsigned char *p = (unsigned char *)malloc(24);
    CHECK(p != NULL);
    memset(p, 0x8B, 24);
    cplus_safe_alloc_meta g1;
    CHECK(cplus_safe_alloc_get_meta(p, &g1) == CPLUS_SAFE_OK);
    unsigned char *q = (unsigned char *)realloc(p, 48);
    CHECK(q != NULL);
    cplus_safe_alloc_meta g2;
    CHECK(cplus_safe_alloc_get_meta(q, &g2) == CPLUS_SAFE_OK);
    CHECK(g2.generation > g1.generation);
    CHECK(cplus_safe_ptr_check_generation(p, g1.generation) == 0);
    CHECK(cplus_safe_ptr_check_generation(q, g2.generation) == 1);
    free(q);
    unsigned char *r = (unsigned char *)malloc(24);
    CHECK(r != NULL);
    cplus_safe_alloc_meta g3;
    CHECK(cplus_safe_alloc_get_meta(r, &g3) == CPLUS_SAFE_OK);
    CHECK(g3.generation > g2.generation);
    CHECK(cplus_safe_ptr_check_generation(r, g1.generation) == 0);
    CHECK(cplus_safe_ptr_check_generation(r, g3.generation) == 1);
    free(r);
    return 0;
}

/* SCENARIO 29: live count/bytes return exactly to zero after full cleanup. */
static int sc_29_accounting_after_cleanup(void)
{
    cplus_safe_alloc_fail_reset();
    CHECK(cplus_safe_alloc_live_count() == 0);
    CHECK(cplus_safe_alloc_live_bytes() == 0);
    void *a = malloc(10);
    void *b = calloc(2, 10);
    void *c = malloc(30);
    CHECK(a && b && c);
    CHECK(cplus_safe_alloc_live_count() == 3);
    CHECK(cplus_safe_alloc_live_bytes() == 60);
    free(a);
    free(b);
    free(c);
    CHECK(cplus_safe_alloc_live_count() == 0);
    CHECK(cplus_safe_alloc_live_bytes() == 0);
    return 0;
}

/* SCENARIO 30: coordinated concurrent alloc/query/free restores baseline. */
typedef struct shared_ctx {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int ready;
    int go;
    int results[4];
} shared_ctx;

typedef struct worker_arg {
    shared_ctx *ctx;
    int idx;
} worker_arg;

static void *alloc_worker(void *opaque)
{
    worker_arg *wa = (worker_arg *)opaque;
    shared_ctx *c = wa->ctx;
    pthread_mutex_lock(&c->mu);
    c->ready++;
    pthread_cond_broadcast(&c->cv);
    while (!c->go) pthread_cond_wait(&c->cv, &c->mu);
    pthread_mutex_unlock(&c->mu);

    int done = 0;
    int i;
    for (i = 0; i < 500; ++i) {
        size_t sz = 8 + (size_t)(i % 61);
        unsigned char *p = (unsigned char *)malloc(sz);
        if (!p) break;
        size_t k;
        for (k = 0; k < sz; ++k) p[k] = (unsigned char)(i + k);
        if (cplus_safe_ptr_size(p) != sz || cplus_safe_ptr_is_live(p) != 1) {
            free(p);
            break;
        }
        free(p);
        done++;
    }
    pthread_mutex_lock(&c->mu);
    c->results[wa->idx] = done;
    pthread_mutex_unlock(&c->mu);
    return NULL;
}

static int sc_30_concurrent_alloc_free(void)
{
    cplus_safe_alloc_fail_reset();
    size_t base = cplus_safe_alloc_live_count();
    size_t baseb = cplus_safe_alloc_live_bytes();
    shared_ctx c;
    CHECK(pthread_mutex_init(&c.mu, NULL) == 0);
    CHECK(pthread_cond_init(&c.cv, NULL) == 0);
    c.ready = 0;
    c.go = 0;
    int i;
    for (i = 0; i < 4; ++i) c.results[i] = -1;
    pthread_t th[4];
    worker_arg wa[4];
    for (i = 0; i < 4; ++i) {
        wa[i].ctx = &c;
        wa[i].idx = i;
        CHECK(pthread_create(&th[i], NULL, alloc_worker, &wa[i]) == 0);
    }
    pthread_mutex_lock(&c.mu);
    while (c.ready < 4) pthread_cond_wait(&c.cv, &c.mu);
    c.go = 1;
    pthread_cond_broadcast(&c.cv);
    pthread_mutex_unlock(&c.mu);
    for (i = 0; i < 4; ++i) {
        CHECK(pthread_join(th[i], NULL) == 0);
        CHECK(c.results[i] == 500);
    }
    CHECK(cplus_safe_alloc_live_count() == base);
    CHECK(cplus_safe_alloc_live_bytes() == baseb);
    pthread_cond_destroy(&c.cv);
    pthread_mutex_destroy(&c.mu);
    return 0;
}

/* ------------------------------------------------------------------------- */

int main(void)
{
    cplus_runtime_set_violation_trap(0);
    cplus_runtime_set_failstop(0);
    cplus_safe_alloc_fail_reset();

    int ran = 0;
#define RUN(fn) do { if ((fn)() != 0) return 1; ++ran; } while (0)
    RUN(sc_01_malloc_zero);
    RUN(sc_02_malloc_size_max);
    RUN(sc_03_malloc_rsize_max);
    RUN(sc_04_inject_zero);
    RUN(sc_05_inject_one);
    RUN(sc_06_inject_many);
    RUN(sc_07_alloc_free_cycles);
    RUN(sc_08_thousand_tiny);
    RUN(sc_09_fragmented_sizes);
    RUN(sc_10_calloc_overflow);
    RUN(sc_11_calloc_zero_count);
    RUN(sc_12_calloc_zero_size);
    RUN(sc_13_calloc_huge);
    RUN(sc_14_realloc_null);
    RUN(sc_15_realloc_zero);
    RUN(sc_16_realloc_grow);
    RUN(sc_17_realloc_move);
    RUN(sc_18_realloc_shrink);
    RUN(sc_19_realloc_failure_preserves);
    RUN(sc_20_realloc_invalid_pointer);
    RUN(sc_21_double_free_alias);
    RUN(sc_22_invalid_free_stack);
    RUN(sc_23_invalid_free_host);
    RUN(sc_24_free_null);
    RUN(sc_25_metadata_query);
    RUN(sc_26_transfer_success);
    RUN(sc_27_transfer_refusal);
    RUN(sc_28_stale_generation);
    RUN(sc_29_accounting_after_cleanup);
    RUN(sc_30_concurrent_alloc_free);
#undef RUN

    printf("runtime_alloc: %d scenarios ok\n", ran);
    return 0;
}
