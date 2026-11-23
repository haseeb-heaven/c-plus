/* F-026: SafeCRT side-table query surface (base, interior and one-past-end
 * pointers, plus untracked stack addresses). */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;
    int anchor = 0x1234;

    f100_begin(&c, "F-026");
    f100_seed(&r, F100_SEED ^ 0x026u);
    base = cplus_safe_alloc_live_count();

    f100_check(&c, "stack_not_live", cplus_safe_ptr_is_live(&anchor) == 0);
    f100_check(&c, "stack_size_zero", cplus_safe_ptr_size(&anchor) == 0u);
    f100_check(&c, "stack_capacity_unknown",
               cplus_safe_remaining_capacity(&anchor) == (size_t)-1);
    f100_check(&c, "stack_was_freed_zero", cplus_safe_ptr_was_freed(&anchor) == 0);

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        size_t want = n == 0u ? 1u : n;
        size_t count = 0u;
        unsigned long long gen;
        cplus_safe_alloc_meta m;
        unsigned char *p = (unsigned char *)malloc_s(n);
        f100_check(&c, "alloc_nonnull", p != NULL);
        if (p == NULL) break;

        (void)cplus_safe_alloc_get_meta(p, &m);
        gen = m.generation;
        f100_check(&c, "gen_check_live",
                   cplus_safe_ptr_check_generation(p, gen) == 1);
        f100_check(&c, "gen_check_wrong",
                   cplus_safe_ptr_check_generation(p, gen + 1ULL) == 0);
        f100_check(&c, "query_length",
                   cplus_safe_query_length(p, 1u, &count) == CPLUS_SAFE_OK &&
                   count == want);
        f100_check(&c, "ptr_count", cplus_safe_ptr_count(p) == want);
        f100_check(&c, "elem_size_one", cplus_safe_ptr_elem_size(p) == 1u);

        if (n > 2u) {
            size_t k = n / 2u;
            f100_check(&c, "interior_capacity",
                       cplus_safe_remaining_capacity(p + k) == want - k);
            f100_check(&c, "one_past_capacity",
                       cplus_safe_remaining_capacity(p + want) == 0u);
            f100_check(&c, "interior_not_base_live",
                       cplus_safe_ptr_is_live(p + k) == 0);
        }
        f100_check(&c, "was_freed_while_live", cplus_safe_ptr_was_freed(p) == 0);
        free_s(p);
        f100_check(&c, "gen_check_after_free",
                   cplus_safe_ptr_check_generation(p, gen) == 0);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
