/* F-036: libc cplus_transfer_ownership pointer-to-pointer cleanup with
 * generation checks and stale-alias refusal. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-036");
    f100_seed(&r, F100_SEED ^ 0x036u);
    base = cplus_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        void *a = cplus_malloc(n == 0u ? 1u : n);
        void *b = NULL;
        void *dst = NULL;
        cplus_alloc_meta m;
        unsigned long long g0, g1;

        f100_check(&c, "alloc_nonnull", a != NULL);
        if (a == NULL) break;
        (void)cplus_alloc_get_meta(a, &m);
        g0 = m.generation;

        f100_check(&c, "transfer_ok",
                   cplus_transfer_ownership(&b, &a) == 0 &&
                   a == NULL && b != NULL);
        (void)cplus_alloc_get_meta(b, &m);
        g1 = m.generation;
        f100_check(&c, "transfer_bumps_gen", g1 > g0);

        /* stale alias: queried only, never dereferenced */
        {
            void *stale = b;
            cplus_free(b);
            b = NULL;
            f100_check(&c, "stale_not_live", cplus_alloc_is_live(stale) == 0);
            f100_check(&c, "stale_gen_rejected",
                       cplus_alloc_check_generation(stale, g1) == 0);
            f100_check(&c, "stale_transfer_refused",
                       cplus_transfer_ownership(&dst, &stale) != 0 &&
                       dst == NULL && stale != NULL);
        }

        /* repeated drop of an already-empty handle is a defined no-op */
        f100_check(&c, "empty_handle_drop",
                   cplus_free_checked(&b) == 0 && b == NULL);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    return f100_end(&c);
}
