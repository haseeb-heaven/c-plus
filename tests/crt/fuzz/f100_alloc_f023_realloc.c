/* F-023: realloc_s growth/shrink/zero-size plus injection at deterministic
 * sequence points; a refused realloc must leave the original owner intact. */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    unsigned char *p;
    size_t base, ops = 0u;
    unsigned long long gen;
    unsigned i;

    f100_begin(&c, "F-023");
    f100_seed(&r, F100_SEED ^ 0x023u);
    base = cplus_safe_alloc_live_count();
    p = (unsigned char *)malloc_s(16u);
    if (p == NULL) { printf("FAIL %s initial_alloc\n", c.tag); return f100_end(&c); }
    gen = 0ULL;
    {
        cplus_safe_alloc_meta m;
        (void)cplus_safe_alloc_get_meta(p, &m);
        gen = m.generation;
    }

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        size_t want = n == 0u ? 1u : n;
        unsigned char *np;
        cplus_safe_alloc_meta m;

        if (i % 127u == 0u) {
            unsigned long long before;
            (void)cplus_safe_alloc_get_meta(p, &m);
            before = m.generation;
            cplus_safe_alloc_fail_after(0); /* next allocation must fail */
            errno = 0;
            np = (unsigned char *)realloc_s(p, 32u);
            f100_check(&c, "injected_realloc_refused",
                       np == NULL && errno == ENOMEM);
            f100_check(&c, "owner_preserved", cplus_safe_ptr_is_live(p) == 1);
            (void)cplus_safe_alloc_get_meta(p, &m);
            f100_check(&c, "generation_unchanged", m.generation == before);
            cplus_safe_alloc_fail_reset();
        }
        np = (unsigned char *)realloc_s(p, n);
        f100_check(&c, "realloc_nonnull", np != NULL);
        if (np == NULL) break;
        p = np;
        f100_check(&c, "resized_size", cplus_safe_ptr_size(p) == want);
        f100_check(&c, "gen_advanced",
                   cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK &&
                   m.generation > gen);
        gen = m.generation;
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    free_s(p);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
