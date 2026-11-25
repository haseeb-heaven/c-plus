/* F-030: libc cplus_malloc deterministic size sweep 0..4096 with zero-size
 * normalization and tracked-size round-trip. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-030");
    f100_seed(&r, F100_SEED ^ 0x030u);
    base = cplus_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        size_t want = n == 0u ? 1u : n;
        unsigned char *p = (unsigned char *)cplus_malloc(n);
        f100_check(&c, "malloc_nonnull", p != NULL);
        if (p == NULL) break;
        f100_check(&c, "zero_size_normalized", cplus_alloc_size(p) == want);
        f100_check(&c, "is_live", cplus_alloc_is_live(p) == 1);
        p[0] = (unsigned char)f100_next(&r);
        p[want - 1u] = p[0];
        f100_check(&c, "remaining_capacity",
                   cplus_alloc_remaining_capacity(p) == want);
        cplus_free(p);
        f100_check(&c, "released", cplus_alloc_is_live(p) == 0);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    return f100_end(&c);
}
