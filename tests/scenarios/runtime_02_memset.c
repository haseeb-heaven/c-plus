/* runtime_02_memset.c -- runtime scenarios for ORDINARY memset().
 *
 * The calls below use the unchanged C spelling from stdlib/safe/safe_c.h;
 * every memset expands to the capacity-checked cplus_memset_s_auto() wrapper.
 * The violation trap is disabled so refusals are observable as errno values
 * (ERANGE overflow, EINVAL null/strict, EFAULT use-after-free) with the
 * destination left untouched, instead of terminating the process.
 *
 * All scenarios avoid deliberately performing genuine UB: refused writes are
 * never dereferenced after free, const-like storage is actually writable, and
 * every over-capacity request is stopped by the checked wrapper.
 */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* safe_c.h remaps malloc/free to the tracked variants.  Two scenarios below
 * need a genuinely untracked host allocation, so drop just those two macros
 * and use explicit helpers; every other allocation calls malloc_s/free_s by
 * name.  The memset mapping is deliberately left in place. */
#undef malloc
#undef free

static void *host_malloc(size_t n) { return malloc(n); }
static void host_free(void *p) { free(p); }

static unsigned char g_memset_global[16];
/* Raw host buffer allocated once, before any tracked block is freed.  A host
 * malloc that reuses a recently freed tracked address is (conservatively)
 * reported as UAF, which would obscure the unknown-capacity checks below. */
static unsigned char *g_host_buf;
/* Writable static storage viewed through a const pointer ("const-like"). */
static unsigned char g_memset_static[8] = {1, 2, 3, 4, 5, 6, 7, 8};

struct memset_canary {
    unsigned char buf[8];
    volatile unsigned long canary;
};

struct memset_flex {
    size_t len;
    unsigned char data[]; /* C99 flexible array member */
};

#define SCENARIO_COUNT 24

/* SCENARIO 1: full exact-size heap fill succeeds. */
static void sc_02_heap_exact(void) {
    unsigned char *p = (unsigned char *)malloc_s(64);
    assert(p != NULL);
    errno = 0;
    memset(p, 0x41, 64);
    assert(errno == 0);
    assert(p[0] == 0x41);
    assert(p[32] == 0x41);
    assert(p[63] == 0x41);
    free_s(p);
}

/* SCENARIO 2: one-past-the-end write is refused (remaining capacity is 0). */
static void sc_02_one_past_end_refused(void) {
    unsigned char *p = (unsigned char *)malloc_s(32);
    assert(p != NULL);
    memset(p, 0x41, 32);
    assert(cplus_safe_remaining_capacity(p + 32) == (size_t)0);
    errno = 0;
    memset(p + 32, 0x43, 8);
    assert(errno == ERANGE);
    assert(p[31] == 0x41); /* byte before the end was not touched */
    free_s(p);
}

/* SCENARIO 3: interior pointer, length greater than remaining capacity. */
static void sc_02_interior_exceeds(void) {
    unsigned char *p = (unsigned char *)malloc_s(64);
    volatile size_t too_big = 17; /* opaque so the refusal is a runtime check */
    assert(p != NULL);
    memset(p, 0x41, 64);
    assert(cplus_safe_remaining_capacity(p + 48) == (size_t)16);
    errno = 0;
    memset(p + 48, 0x42, too_big);
    assert(errno == ERANGE);
    assert(p[48] == 0x41); /* refused write left the interior untouched */
    free_s(p);
}

/* SCENARIO 4: interior pointer with exactly the remaining capacity. */
static void sc_02_interior_exact(void) {
    unsigned char *p = (unsigned char *)malloc_s(64);
    assert(p != NULL);
    memset(p, 0x41, 64);
    errno = 0;
    memset(p + 48, 0x42, 16);
    assert(errno == 0);
    assert(p[47] == 0x41);
    assert(p[48] == 0x42 && p[63] == 0x42);
    free_s(p);
}

/* SCENARIO 5: zero length is a defined no-op, including at one-past-end. */
static void sc_02_zero_length(void) {
    unsigned char *p = (unsigned char *)malloc_s(8);
    assert(p != NULL);
    memset(p, 0x41, 8);
    errno = 0;
    memset(p, 0x42, 0);
    assert(errno == 0);
    assert(p[0] == 0x41 && p[7] == 0x41);
    errno = 0;
    memset(p + 8, 0x43, 0); /* zero length short-circuits capacity checks */
    assert(errno == 0);
    assert(p[7] == 0x41);
    free_s(p);
}

/* SCENARIO 6: NULL destination is refused (memset checks NULL before n==0). */
static void sc_02_null_dest(void) {
    errno = 0;
    memset(NULL, 0x41, 4);
    assert(errno == EINVAL);
    errno = 0;
    memset(NULL, 0x41, 0);
    assert(errno == EINVAL);
}

/* SCENARIO 7: stack array with a compile-time known object size. */
static void sc_02_stack_object(void) {
    unsigned char stack[24];
    volatile size_t too_big = sizeof stack + 1;
    size_t i;
    memset(stack, 0x33, sizeof stack);
    for (i = 0; i < sizeof stack; ++i) assert(stack[i] == 0x33);
    errno = 0;
    memset(stack, 0x34, too_big);
    assert(errno == ERANGE);
    assert(stack[0] == 0x33 && stack[23] == 0x33);
}

/* SCENARIO 8: struct member fill must not touch the trailing canary. */
static void sc_02_struct_canary(void) {
    struct memset_canary s;
    volatile size_t too_big = sizeof s.buf + 1;
    size_t i;
    s.canary = 0x12345678UL;
    errno = 0;
    memset(s.buf, 0x5A, sizeof s.buf);
    assert(errno == 0);
    for (i = 0; i < sizeof s.buf; ++i) assert(s.buf[i] == 0x5A);
    assert(s.canary == 0x12345678UL);
    errno = 0;
    memset(s.buf, 0x5B, too_big);
    assert(errno == ERANGE);
    assert(s.buf[0] == 0x5A);
    assert(s.canary == 0x12345678UL);
}

/* SCENARIO 9: file-scope (global) array with known object size. */
static void sc_02_global_array(void) {
    volatile size_t too_big = sizeof g_memset_global + 1;
    size_t i;
    errno = 0;
    memset(g_memset_global, 0x61, sizeof g_memset_global);
    assert(errno == 0);
    for (i = 0; i < sizeof g_memset_global; ++i)
        assert(g_memset_global[i] == 0x61);
    errno = 0;
    memset(g_memset_global, 0x62, too_big);
    assert(errno == ERANGE);
    assert(g_memset_global[0] == 0x61);
    assert(g_memset_global[15] == 0x61);
}

/* SCENARIO 10: SIZE_MAX request is refused without performing any write. */
static void sc_02_size_max(void) {
    unsigned char *p = (unsigned char *)malloc_s(16);
    volatile size_t huge = SIZE_MAX;
    assert(p != NULL);
    memset(p, 0x41, 16);
    errno = 0;
    memset(p, 0x42, huge);
    assert(errno == ERANGE);
    assert(p[0] == 0x41 && p[15] == 0x41);
    free_s(p);
}

/* SCENARIO 11: negative int values are promoted/converted to unsigned char. */
static void sc_02_negative_value(void) {
    unsigned char *p = (unsigned char *)malloc_s(8);
    size_t i;
    assert(p != NULL);
    errno = 0;
    memset(p, -1, 8);
    assert(errno == 0);
    for (i = 0; i < 8; ++i) assert(p[i] == 0xFF);
    memset(p, -2, 4);
    for (i = 0; i < 4; ++i) assert(p[i] == 0xFE);
    assert(p[4] == 0xFF); /* second fill stopped at n bytes */
    free_s(p);
}

/* SCENARIO 12: fill with 0 zeroes every byte. */
static void sc_02_fill_zero(void) {
    unsigned char *p = (unsigned char *)malloc_s(12);
    size_t i;
    assert(p != NULL);
    memset(p, 0x77, 12);
    errno = 0;
    memset(p, 0, 12);
    assert(errno == 0);
    for (i = 0; i < 12; ++i) assert(p[i] == 0);
    free_s(p);
}

/* SCENARIO 13: fill with 0xFF (positive byte value). */
static void sc_02_fill_ff(void) {
    unsigned char stack[10];
    size_t i;
    errno = 0;
    memset(stack, 0xFF, sizeof stack);
    assert(errno == 0);
    for (i = 0; i < sizeof stack; ++i) assert(stack[i] == 0xFF);
}

/* SCENARIO 14: tracked allocation after realloc grew; new capacity accepted. */
static void sc_02_realloc_grew(void) {
    unsigned char *p = (unsigned char *)malloc_s(8);
    volatile size_t too_big = 65;
    size_t i;
    assert(p != NULL);
    memset(p, 0x11, 8);
    p = (unsigned char *)realloc_s(p, 64);
    assert(p != NULL);
    assert(cplus_safe_remaining_capacity(p) == (size_t)64);
    errno = 0;
    memset(p, 0x22, 64);
    assert(errno == 0);
    for (i = 0; i < 64; ++i) assert(p[i] == 0x22);
    errno = 0;
    memset(p, 0x23, too_big);
    assert(errno == ERANGE);
    assert(p[63] == 0x22);
    free_s(p);
}

/* SCENARIO 15: tracked allocation after realloc shrank; old capacity refused. */
static void sc_02_realloc_shrank(void) {
    unsigned char *p = (unsigned char *)malloc_s(64);
    volatile size_t too_big = 9;
    size_t i;
    assert(p != NULL);
    memset(p, 0x11, 64);
    p = (unsigned char *)realloc_s(p, 8);
    assert(p != NULL);
    assert(cplus_safe_remaining_capacity(p) == (size_t)8);
    errno = 0;
    memset(p, 0x24, 8);
    assert(errno == 0);
    for (i = 0; i < 8; ++i) assert(p[i] == 0x24);
    errno = 0;
    memset(p, 0x25, too_big);
    assert(errno == ERANGE);
    assert(p[7] == 0x24); /* do not read past the shrunken block */
    free_s(p);
}

/* SCENARIO 16: host malloc has unknown capacity; default mode fills + warns. */
static void sc_02_host_malloc_default(void) {
    unsigned char *raw = g_host_buf;
    assert(raw != NULL);
    assert(cplus_safe_remaining_capacity(raw) == (size_t)-1);
    cplus_safe_set_unknown_capacity_mode(0);
    errno = 0;
    memset(raw, 0x5A, 16);
    assert(errno == 0);
    assert(raw[0] == 0x5A && raw[15] == 0x5A);
}

/* SCENARIO 17: the same unknown-capacity pointer is refused in strict mode. */
static void sc_02_host_malloc_strict(void) {
    unsigned char *raw = g_host_buf;
    assert(raw != NULL);
    cplus_safe_set_unknown_capacity_mode(0);
    memset(raw, 0x11, 16);
    assert(cplus_safe_remaining_capacity(raw) == (size_t)-1);
    cplus_safe_set_unknown_capacity_mode(1);
    errno = 0;
    memset(raw, 0x5B, 16);
    assert(errno == EINVAL);
    assert(raw[0] == 0x11 && raw[15] == 0x11); /* refused, untouched */
    cplus_safe_set_unknown_capacity_mode(0);
    host_free(raw);
}

/* SCENARIO 18: static const-like storage (writable object via const view). */
static void sc_02_static_constlike(void) {
    const unsigned char *ro = g_memset_static;
    unsigned char *w = (unsigned char *)ro; /* points at writable static data */
    volatile size_t too_big = sizeof g_memset_static + 1;
    errno = 0;
    memset(w, 0x77, sizeof g_memset_static);
    assert(errno == 0);
    assert(g_memset_static[0] == 0x77 && g_memset_static[7] == 0x77);
    cplus_safe_set_unknown_capacity_mode(1);
    errno = 0;
    memset(w, 0x11, too_big);
    /* Known dmax => ERANGE; unknown dmax in strict mode => EINVAL. */
    assert(errno == ERANGE || errno == EINVAL);
    assert(g_memset_static[0] == 0x77 && g_memset_static[7] == 0x77);
    cplus_safe_set_unknown_capacity_mode(0);
}

/* SCENARIO 19: destination in a flexible array member; tracked capacity is
 * exactly the trailing bytes of the allocation. */
static void sc_02_flexible_array(void) {
    struct memset_flex *f;
    volatile size_t too_big = 17;
    f = (struct memset_flex *)malloc_s(sizeof *f + 16);
    assert(f != NULL);
    f->len = 16;
    assert(cplus_safe_remaining_capacity(f->data) == (size_t)16);
    errno = 0;
    memset(f->data, 0x3C, 16);
    assert(errno == 0);
    assert(f->data[0] == 0x3C && f->data[15] == 0x3C);
    errno = 0;
    memset(f->data, 0x3D, too_big);
    assert(errno == ERANGE);
    assert(f->data[0] == 0x3C);
    free_s(f);
}

/* SCENARIO 20: value > UCHAR_MAX is rejected; tiny destination untouched. */
static void sc_02_huge_value_tiny_dest(void) {
    unsigned char tiny[4] = {0xA5, 0xA5, 0xA5, 0xA5};
    errno = 0;
    memset(tiny, 0x1FF, sizeof tiny);
    /* safeclib reports ESLEMAX for value > 255; the adapter maps it to EINVAL. */
    assert(errno == EINVAL);
    assert(tiny[0] == 0xA5 && tiny[1] == 0xA5);
    assert(tiny[2] == 0xA5 && tiny[3] == 0xA5);
}

/* SCENARIO 21: repeated partial fills stay in bounds and compose. */
static void sc_02_repeated_partial(void) {
    unsigned char *p = (unsigned char *)malloc_s(32);
    size_t i;
    assert(p != NULL);
    errno = 0;
    for (i = 0; i < 4; ++i) {
        memset(p + i * 8, (int)(0x40 + i), 8);
        assert(errno == 0);
    }
    for (i = 0; i < 4; ++i) {
        size_t j;
        for (j = 0; j < 8; ++j)
            assert(p[i * 8 + j] == (unsigned char)(0x40 + i));
    }
    free_s(p);
}

/* SCENARIO 22: fill after free is refused as UAF (EFAULT), without a crash. */
static void sc_02_use_after_free(void) {
    unsigned char *p = (unsigned char *)malloc_s(16);
    assert(p != NULL);
    memset(p, 0x41, 16);
    free_s(p);
    assert(cplus_safe_ptr_was_freed(p) == 1);
    errno = 0;
    memset(p, 0x42, 4);
    assert(errno == EFAULT);
    /* Freed storage is intentionally not read back: that would be real UB. */
}

/* SCENARIO 23: alias after ownership transfer still fills the live block;
 * memset's capacity check is address-based, not generation-based. */
static void sc_02_alias_after_transfer(void) {
    void *owner = malloc_s(16);
    void *alias;
    void *dst = NULL;
    assert(owner != NULL);
    alias = owner;
    assert(cplus_safe_transfer_ownership(&dst, &owner) == 0);
    assert(owner == NULL);
    assert(dst == alias);
    errno = 0;
    memset((unsigned char *)dst, 0x5E, 16);
    assert(errno == 0);
    assert(((unsigned char *)alias)[0] == 0x5E);
    errno = 0;
    memset((unsigned char *)alias, 0x5F, 16);
    assert(errno == 0);
    assert(((unsigned char *)dst)[15] == 0x5F);
    free_s(dst);
}

/* SCENARIO 24: interleaved logging on/off yields identical memset results. */
static void sc_02_logging_toggle(void) {
    unsigned char *a = (unsigned char *)malloc_s(8);
    unsigned char *b = (unsigned char *)malloc_s(8);
    size_t i;
    assert(a != NULL && b != NULL);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    assert(cplus_safe_log_get_level() == CPLUS_SAFE_LOG_OFF);
    errno = 0;
    memset(a, 0x63, 8);
    assert(errno == 0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    assert(cplus_safe_log_get_level() == CPLUS_SAFE_LOG_TRACE);
    errno = 0;
    memset(b, 0x63, 8);
    assert(errno == 0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    for (i = 0; i < 8; ++i) assert(a[i] == b[i] && a[i] == 0x63);
    free_s(a);
    free_s(b);
}

int main(void) {
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_unknown_capacity_mode(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Acquire the untracked buffer first: no tracked block has been freed yet,
     * so its address cannot collide with the recent-free cache. */
    g_host_buf = (unsigned char *)host_malloc(16);
    assert(g_host_buf != NULL);

    sc_02_heap_exact();
    sc_02_one_past_end_refused();
    sc_02_interior_exceeds();
    sc_02_interior_exact();
    sc_02_zero_length();
    sc_02_null_dest();
    sc_02_stack_object();
    sc_02_struct_canary();
    sc_02_global_array();
    sc_02_size_max();
    sc_02_negative_value();
    sc_02_fill_zero();
    sc_02_fill_ff();
    /* Run the interior-pointer loop before any block is freed: the bounded
     * recent-free cache can otherwise misclassify an interior address that
     * coincides with a stale freed-block address as use-after-free. */
    sc_02_repeated_partial();
    sc_02_realloc_grew();
    sc_02_realloc_shrank();
    sc_02_host_malloc_default();
    sc_02_host_malloc_strict();
    sc_02_static_constlike();
    sc_02_flexible_array();
    sc_02_huge_value_tiny_dest();
    sc_02_use_after_free();
    sc_02_alias_after_transfer();
    sc_02_logging_toggle();

    printf("runtime_memset: %d scenarios ok\n", SCENARIO_COUNT);
    return 0;
}
