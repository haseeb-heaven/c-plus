/* F-038: repeated drop cycles across both allocator families, checking
 * side-table balance, generation monotonicity and stale-alias refusal. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t safe_base, libc_base, ops = 0u;
    unsigned long long last_safe_gen = 0ULL, last_libc_gen = 0ULL;
    unsigned i;

    f100_begin(&c, "F-038");
    f100_seed(&r, F100_SEED ^ 0x038u);
    safe_base = cplus_safe_alloc_live_count();
    libc_base = cplus_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        size_t want = n == 0u ? 1u : n;
        unsigned char *sp = (unsigned char *)malloc_s(n);
        unsigned char *lp = (unsigned char *)cplus_malloc(n);
        cplus_safe_alloc_meta sm;
        cplus_alloc_meta lm;
        unsigned long long sg, lg;
        void *sh, *lh, *lh_stale;

        f100_check(&c, "both_alloc", sp != NULL && lp != NULL);
        if (sp == NULL || lp == NULL) break;

        (void)cplus_safe_alloc_get_meta(sp, &sm);
        (void)cplus_alloc_get_meta(lp, &lm);
        sg = sm.generation;
        lg = lm.generation;
        f100_check(&c, "gens_monotonic",
                   sg > last_safe_gen && lg > last_libc_gen);
        last_safe_gen = sg;
        last_libc_gen = lg;
        f100_check(&c, "sizes_match",
                   cplus_safe_ptr_size(sp) == want &&
                   cplus_alloc_size(lp) == want);

        sh = sp;      /* stale SafeCRT alias, side-table queries only */
        lh = lp;      /* handle consumed by libc pointer-to-pointer free */
        lh_stale = lp; /* stale libc alias, side-table queries only */
        f100_check(&c, "checked_release",
                   free_checked_s(sp) == CPLUS_SAFE_OK &&
                   cplus_free_checked(&lh) == 0 && lh == NULL);
        sp = NULL;

        /* repeated drop: both refusals, no dereference of either stale alias */
        f100_check(&c, "repeated_drop_safe",
                   free_checked_s(sh) == CPLUS_SAFE_EINVAL &&
                   cplus_safe_ptr_is_live(sh) == 0 &&
                   cplus_safe_ptr_check_generation(sh, sg) == 0);
        f100_check(&c, "repeated_drop_libc",
                   cplus_free_checked(&lh_stale) != 0 &&
                   cplus_alloc_is_live(lh_stale) == 0 &&
                   cplus_alloc_check_generation(lp, lg) == 0);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "safe_live_balance",
               cplus_safe_alloc_live_count() == safe_base);
    f100_check(&c, "libc_live_balance",
               cplus_alloc_live_count() == libc_base);
    return f100_end(&c);
}
