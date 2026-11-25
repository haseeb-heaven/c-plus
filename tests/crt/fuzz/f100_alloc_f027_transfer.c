/* F-027: pointer-to-pointer ownership transfer with generation checks and
 * refusal for a stale alias (queried only through the side table). */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-027");
    f100_seed(&r, F100_SEED ^ 0x027u);
    base = cplus_safe_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        void *a = malloc_s(n == 0u ? 1u : n);
        void *b = NULL;
        void *cc = NULL;
        cplus_safe_alloc_meta m;
        unsigned long long g0, g1, g2;

        f100_check(&c, "alloc_nonnull", a != NULL);
        if (a == NULL) break;
        (void)cplus_safe_alloc_get_meta(a, &m);
        g0 = m.generation;

        f100_check(&c, "transfer_a_to_b",
                   cplus_safe_transfer_ownership(&b, &a) == CPLUS_SAFE_OK &&
                   a == NULL && b != NULL);
        (void)cplus_safe_alloc_get_meta(b, &m);
        g1 = m.generation;
        f100_check(&c, "transfer_bumps_gen", g1 > g0);

        f100_check(&c, "transfer_b_to_c",
                   cplus_safe_transfer_ownership(&cc, &b) == CPLUS_SAFE_OK &&
                   b == NULL && cc != NULL);
        (void)cplus_safe_alloc_get_meta(cc, &m);
        g2 = m.generation;
        f100_check(&c, "transfer_bumps_gen_again", g2 > g1);

        /* Refuse a transfer into an already-occupied destination. */
        {
            void *dst = cc;
            void *src = malloc_s(8u);
            f100_check(&c, "extra_alloc", src != NULL);
            if (src != NULL) {
                f100_check(&c, "occupied_dst_refused",
                           cplus_safe_transfer_ownership(&dst, &src) ==
                               CPLUS_SAFE_EINVAL &&
                           dst == cc && src != NULL);
                f100_check(&c, "extra_src_gen_old",
                           cplus_safe_ptr_check_generation(
                               src, 0ULL) == 0);
                free_s(src);
            }
        }

        free_s(cc); /* single owner released once */
        f100_check(&c, "owner_released", cplus_safe_ptr_is_live(cc) == 0);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
