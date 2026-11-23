/* F-024: free_s / free_checked_s double-free and invalid-free refusal.
 * Stale pointers are only queried through the side table, never dereferenced. */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;
    int anchor = 0x5A5A;

    f100_begin(&c, "F-024");
    f100_seed(&r, F100_SEED ^ 0x024u);
    base = cplus_safe_alloc_live_count();

    /* Focused recent-free-cache check, run before the long sweep so no other
     * free can evict the bucket. */
    {
        void *one = malloc_s(24u);
        void *stale = one;
        cplus_safe_alloc_meta m;
        f100_check(&c, "focused_alloc", one != NULL);
        if (one != NULL) {
            free_s(one);
            f100_check(&c, "focused_not_live", cplus_safe_ptr_is_live(stale) == 0);
            f100_check(&c, "focused_was_freed", cplus_safe_ptr_was_freed(stale) == 1);
            f100_check(&c, "focused_meta_reject",
                       cplus_safe_alloc_get_meta(stale, &m) != CPLUS_SAFE_OK);
            f100_check(&c, "focused_gen_zero",
                       cplus_safe_ptr_check_generation(stale, 1ULL) == 0);
            f100_check(&c, "focused_double_refused",
                       free_checked_s(stale) == CPLUS_SAFE_EINVAL);
        }
    }

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        switch (i % 5u) {
        case 0u: {
            void *p = malloc_s(n == 0u ? 1u : n);
            void *stale = p;
            f100_check(&c, "case0_alloc", p != NULL);
            if (p == NULL) break;
            free_s(p);
            f100_check(&c, "case0_not_live", cplus_safe_ptr_is_live(stale) == 0);
            f100_check(&c, "case0_repeat_drop_refused",
                       free_checked_s(stale) == CPLUS_SAFE_EINVAL);
            break;
        }
        case 1u:
            free_s(NULL); /* defined no-op */
            c.checks++;
            break;
        case 2u:
            f100_check(&c, "case2_stack_free_refused",
                       free_checked_s(&anchor) == CPLUS_SAFE_EINVAL &&
                       anchor == 0x5A5A);
            break;
        case 3u: {
            unsigned char *p = (unsigned char *)malloc_s(32u);
            f100_check(&c, "case3_alloc", p != NULL);
            if (p != NULL) {
                f100_check(&c, "case3_interior_refused",
                           free_checked_s(p + 1) == CPLUS_SAFE_EINVAL &&
                           cplus_safe_ptr_is_live(p) == 1);
                free_s(p);
            }
            break;
        }
        default: {
            void *p = malloc_s(n == 0u ? 1u : n);
            f100_check(&c, "case4_alloc", p != NULL);
            if (p != NULL) {
                f100_check(&c, "case4_checked_ok",
                           free_checked_s(p) == CPLUS_SAFE_OK);
                p = NULL; /* pointer-to-pointer style cleanup by the caller */
            }
            break;
        }
        }
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
