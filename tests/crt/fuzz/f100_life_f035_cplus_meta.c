/* F-035: libc side-table query surface for base, interior, one-past-end and
 * untracked addresses. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;
    int anchor = 0x1234;

    f100_begin(&c, "F-035");
    f100_seed(&r, F100_SEED ^ 0x035u);
    base = cplus_alloc_live_count();

    f100_check(&c, "stack_not_live", cplus_alloc_is_live(&anchor) == 0);
    f100_check(&c, "stack_size_zero", cplus_alloc_size(&anchor) == 0u);
    f100_check(&c, "stack_capacity_unknown",
               cplus_alloc_remaining_capacity(&anchor) == (size_t)-1);
    {
        cplus_alloc_meta m;
        f100_check(&c, "stack_meta_reject", cplus_alloc_get_meta(&anchor, &m) != 0);
    }

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        size_t want = n == 0u ? 1u : n;
        size_t count = 999u;
        unsigned long long gen;
        cplus_alloc_meta m;
        unsigned char *p = (unsigned char *)cplus_malloc(n);
        f100_check(&c, "alloc_nonnull", p != NULL);
        if (p == NULL) break;

        (void)cplus_alloc_get_meta(p, &m);
        gen = m.generation;
        f100_check(&c, "gen_check_live",
                   cplus_alloc_check_generation(p, gen) == 1);
        f100_check(&c, "gen_check_wrong",
                   cplus_alloc_check_generation(p, gen + 1ULL) == 0);
        f100_check(&c, "query_length",
                   cplus_alloc_query_length(p, 1u, &count) == 0 && count == want);
        f100_check(&c, "length", cplus_alloc_length(p) == want);
        f100_check(&c, "elem_size_one", cplus_alloc_elem_size(p) == 1u);
        f100_check(&c, "meta_live",
                   cplus_alloc_get_meta(p, &m) == 0 && m.is_live == 1 &&
                   m.byte_size == want && m.generation == gen);

        if (n > 2u) {
            size_t k = n / 2u;
            f100_check(&c, "interior_capacity",
                       cplus_alloc_remaining_capacity(p + k) == want - k);
            f100_check(&c, "one_past_capacity",
                       cplus_alloc_remaining_capacity(p + want) == 0u);
            f100_check(&c, "interior_not_base_live",
                       cplus_alloc_is_live(p + k) == 0);
        }
        cplus_free(p);
        f100_check(&c, "gen_check_after_free",
                   cplus_alloc_check_generation(p, gen) == 0);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    return f100_end(&c);
}
