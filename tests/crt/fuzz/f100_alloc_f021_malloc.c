/* F-021: malloc_s deterministic size sweep 0..4096 with zero-size
 * normalization and metadata round-trip. */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-021");
    f100_seed(&r, F100_SEED ^ 0x021u);
    base = cplus_safe_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        size_t want = n == 0u ? 1u : n;
        cplus_safe_alloc_meta m;
        unsigned char *p = (unsigned char *)malloc_s(n);
        f100_check(&c, "malloc_nonnull", p != NULL);
        if (p == NULL) break;

        f100_check(&c, "zero_size_normalized", cplus_safe_ptr_size(p) == want);
        p[0] = (unsigned char)f100_next(&r);
        p[want - 1u] = p[0]; /* always inside the tracked block */
        f100_check(&c, "meta_live",
                   cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK &&
                   m.is_live == 1 && m.byte_size == want &&
                   m.generation != 0ULL);
        f100_check(&c, "remaining_capacity",
                   cplus_safe_remaining_capacity(p) == want);
        free_s(p);
        f100_check(&c, "released_not_live", cplus_safe_ptr_is_live(p) == 0);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
