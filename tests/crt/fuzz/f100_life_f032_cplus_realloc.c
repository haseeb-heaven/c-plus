/* F-032: libc cplus_realloc growth/shrink/zero-size with deterministic
 * injection; a refused resize leaves original owner and generation intact. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    unsigned char *p;
    size_t base, ops = 0u;
    unsigned long long gen;
    unsigned i;

    f100_begin(&c, "F-032");
    f100_seed(&r, F100_SEED ^ 0x032u);
    base = cplus_alloc_live_count();
    p = (unsigned char *)cplus_malloc(16u);
    if (p == NULL) { printf("FAIL %s initial_alloc\n", c.tag); return f100_end(&c); }
    {
        cplus_alloc_meta m;
        (void)cplus_alloc_get_meta(p, &m);
        gen = m.generation;
    }

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        size_t want = n == 0u ? 1u : n;
        unsigned char *np;
        cplus_alloc_meta m;

        if (i % 127u == 0u) {
            unsigned long long before;
            (void)cplus_alloc_get_meta(p, &m);
            before = m.generation;
            cplus_alloc_fail_after(0);
            errno = 0;
            np = (unsigned char *)cplus_realloc(p, 32u);
            f100_check(&c, "injected_realloc_refused",
                       np == NULL && errno == ENOMEM);
            f100_check(&c, "owner_preserved", cplus_alloc_is_live(p) == 1);
            (void)cplus_alloc_get_meta(p, &m);
            f100_check(&c, "generation_unchanged", m.generation == before);
            cplus_alloc_fail_reset();
        }
        np = (unsigned char *)cplus_realloc(p, n);
        f100_check(&c, "realloc_nonnull", np != NULL);
        if (np == NULL) break;
        p = np;
        f100_check(&c, "resized_size", cplus_alloc_size(p) == want);
        f100_check(&c, "gen_advanced",
                   cplus_alloc_get_meta(p, &m) == 0 && m.generation > gen);
        gen = m.generation;
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    cplus_free(p);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    return f100_end(&c);
}
