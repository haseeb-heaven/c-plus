/* Deterministic fuzz coverage: checked ORDINARY-NAME allocation wrappers.
 *
 * malloc/calloc/realloc/free are used through the ordinary-name macros of
 * stdlib/safe/safe_c.h (malloc_s/calloc_s/realloc_s/free_s underneath).
 * Sizes are bounded to [0, 512]. Exact-size blocks are verified with the
 * public metadata API, interior/one-past-end capacities are checked, and a
 * one-over memset is asserted to be refused with errno == ERANGE while every
 * in-block byte stays unchanged. Realloc preserves bytes on grow, shrink and
 * injected-failure paths. Generation counters are deliberately NOT hashed
 * because they are process-global and would differ between logging passes.
 */
#include "safec_layer_common.h"

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_runtime.h"

static unsigned long g_ops;

static size_t alloc_size_for(int iter) {
    switch (iter & 7) {
        case 0: return 0u;                                    /* defined 1-byte block */
        case 1: return 1u;
        case 2: return SC_LEN_MAX;                            /* exact cap */
        default: return 1u + (size_t)(sc_rnd() % SC_LEN_MAX); /* bounded random */
    }
}

static void run_iteration(int iter) {
    size_t s = alloc_size_for(iter);
    size_t cap = (s == 0u) ? 1u : s;

    /* 1. malloc_s via ordinary malloc, exact-fit fill, one-over refusal. */
    {
        cplus_safe_alloc_meta meta;
        unsigned char *p = (unsigned char *)malloc(s);
        int pat = (int)(sc_rnd() & 0xFFu);
        size_t i;
        assert(p != NULL);
        assert(cplus_safe_alloc_get_meta(p, &meta) == CPLUS_SAFE_OK);
        assert(meta.is_live == 1 && meta.byte_size == cap);
        assert(meta.elem_size == 1u && meta.elem_count == cap);
        assert(meta.generation != 0u);
        assert(cplus_safe_ptr_is_live(p) == 1);
        assert(cplus_safe_ptr_size(p) == cap);
        assert(cplus_safe_remaining_capacity(p) == cap);
        assert(cplus_safe_remaining_capacity(p + cap / 2u) == cap - cap / 2u);
        assert(cplus_safe_remaining_capacity(p + cap) == 0u);

        errno = 0;
        memset(p, pat, cap);
        assert(errno == 0);
        for (i = 0; i < cap; ++i) assert(p[i] == (unsigned char)pat);
        errno = 0;
        memset(p, (pat + 1) & 0xFF, cap + 1u);
        assert(errno == ERANGE);
        for (i = 0; i < cap; ++i) assert(p[i] == (unsigned char)pat);

        sc_hash_tag("M");
        sc_hash_u64((uint64_t)s);
        sc_hash_u64((uint64_t)cap);
        sc_hash_u64((uint64_t)pat);
        sc_hash_u64((uint64_t)meta.is_live);
        sc_hash_u64((uint64_t)meta.elem_size);
        sc_hash(p, cap);

        free(p); /* -> free_s */
        assert(cplus_safe_ptr_is_live(p) == 0);
        assert(cplus_safe_ptr_was_freed(p) == 1);
        errno = 0;
        free(p); /* second free is safely refused */
        assert(cplus_safe_ptr_is_live(p) == 0);
        assert(free_checked_s(p) != CPLUS_SAFE_OK);
        ++g_ops;
    }

    /* 2. calloc_s via ordinary calloc: zeroing + element metadata. */
    if (iter % 3 == 0) {
        size_t count = 1u + (size_t)(sc_rnd() % 8u);
        size_t elem = 1u + (size_t)(sc_rnd() % 16u);
        cplus_safe_alloc_meta meta;
        unsigned char *q;
        while (count * elem > SC_LEN_MAX) elem /= 2u;
        assert(count * elem >= 1u && count * elem <= SC_LEN_MAX);
        q = (unsigned char *)calloc(count, elem);
        assert(q != NULL);
        assert(cplus_safe_alloc_get_meta(q, &meta) == CPLUS_SAFE_OK);
        assert(meta.is_live == 1 && meta.byte_size == count * elem);
        assert(meta.elem_size == elem && meta.elem_count == count);
        {
            size_t i;
            for (i = 0; i < count * elem; ++i) assert(q[i] == 0u);
            errno = 0;
            memset(q, 0x7Fu, count * elem + 1u);
            assert(errno == ERANGE);
            for (i = 0; i < count * elem; ++i) assert(q[i] == 0u);
        }
        sc_hash_tag("Z");
        sc_hash_u64((uint64_t)count);
        sc_hash_u64((uint64_t)elem);
        sc_hash_u64((uint64_t)meta.byte_size);
        free(q);
        ++g_ops;
    }

    /* 3. realloc_s via ordinary realloc: grow, shrink, injected failure. */
    if (iter % 4 == 0) {
        size_t a = 32u + (size_t)(sc_rnd() % 64u);
        unsigned char *p = (unsigned char *)malloc(a);
        unsigned char *q, *r, *failed;
        int pat = (int)(sc_rnd() & 0xFFu);
        size_t i;
        assert(p != NULL);
        errno = 0;
        memset(p, pat, a);
        assert(errno == 0);

        q = (unsigned char *)realloc(p, a * 2u);
        assert(q != NULL && cplus_safe_ptr_is_live(q) == 1);
        assert(cplus_safe_ptr_size(q) == a * 2u);
        assert(cplus_safe_ptr_is_live(p) == 0);
        for (i = 0; i < a; ++i) assert(q[i] == (unsigned char)pat);

        r = (unsigned char *)realloc(q, a / 2u);
        assert(r != NULL && cplus_safe_ptr_is_live(r) == 1);
        assert(cplus_safe_ptr_size(r) == a / 2u);
        for (i = 0; i < a / 2u; ++i) assert(r[i] == (unsigned char)pat);

        cplus_safe_alloc_fail_after(0);
        errno = 0;
        failed = (unsigned char *)realloc(r, a);
        assert(failed == NULL);
        assert(errno == ENOMEM);
        assert(cplus_safe_ptr_is_live(r) == 1);
        assert(cplus_safe_ptr_size(r) == a / 2u);
        for (i = 0; i < a / 2u; ++i) assert(r[i] == (unsigned char)pat);
        cplus_safe_alloc_fail_reset();

        sc_hash_tag("R");
        sc_hash_u64((uint64_t)a);
        sc_hash_u64((uint64_t)pat);
        sc_hash_u64((uint64_t)cplus_safe_ptr_size(r));
        sc_hash(r, a / 2u);
        free(r);
        assert(cplus_safe_ptr_is_live(r) == 0);
        ++g_ops;
    }
}

static uint64_t run_pass(int logging_on) {
    int it;
    g_ops = 0u;
    sc_seed();
    sc_digest_init();
    cplus_safe_log_set_level(logging_on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
    for (it = 0; it < SC_ITERATIONS; ++it) run_iteration(it);
    return sc_digest_g;
}

int main(void) {
    uint64_t digest_off, digest_on;
    unsigned long ops_off = 0u, ops_on = 0u;

    setvbuf(stdout, NULL, _IONBF, 0);
    sc_install_silent_handler();
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_unknown_capacity_mode(1);
    assert(cplus_safe_alloc_live_count() == 0u);

    sc_silence_stderr();
    digest_off = run_pass(0);
    ops_off = g_ops;
    digest_on = run_pass(1);
    ops_on = g_ops;
    sc_restore_stderr();

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    assert(digest_off == digest_on);
    assert(ops_off == ops_on);
    assert(cplus_safe_alloc_live_count() == 0u); /* no leaks across either pass */
    printf("safec_layer_alloc: digest=%016llx\n", (unsigned long long)digest_off);
    printf("safec_layer_alloc: %lu operations, 2 logging passes, parity ok\n", ops_off);
    return 0;
}
