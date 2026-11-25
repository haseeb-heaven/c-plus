/* F-033: cplus_free / cplus_free_checked refusal paths (double, invalid,
 * interior) with pointer-to-pointer cleanup.  Stale pointers are queried via
 * the side table and never dereferenced. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned long dbl0, inv0;
    unsigned i;
    int anchor = 0x5A5A;

    f100_begin(&c, "F-033");
    f100_seed(&r, F100_SEED ^ 0x033u);
    base = cplus_alloc_live_count();
    dbl0 = cplus_alloc_double_free_count();
    inv0 = cplus_alloc_invalid_free_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        switch (i % 5u) {
        case 0u: {
            void *p = cplus_malloc(n == 0u ? 1u : n);
            void *stale = p;
            void *handle = p;
            f100_check(&c, "case0_alloc", p != NULL);
            if (p == NULL) break;
            cplus_free(p);
            f100_check(&c, "case0_not_live", cplus_alloc_is_live(stale) == 0);
            f100_check(&c, "case0_double_refused",
                       cplus_free_checked(&handle) != 0 && handle == NULL);
            f100_check(&c, "case0_double_counted",
                       cplus_alloc_double_free_count() == dbl0 + 1u);
            dbl0 = cplus_alloc_double_free_count();
            break;
        }
        case 1u:
            cplus_free(NULL);
            c.checks++;
            break;
        case 2u: {
            void *h = &anchor;
            f100_check(&c, "case2_stack_refused",
                       cplus_free_checked(&h) != 0 && h == NULL &&
                       anchor == 0x5A5A);
            f100_check(&c, "case2_invalid_counted",
                       cplus_alloc_invalid_free_count() > inv0);
            inv0 = cplus_alloc_invalid_free_count();
            break;
        }
        case 3u: {
            unsigned char *p = (unsigned char *)cplus_malloc(32u);
            void *h = p == NULL ? NULL : (void *)(p + 1);
            f100_check(&c, "case3_alloc", p != NULL);
            if (p != NULL) {
                f100_check(&c, "case3_interior_refused",
                           cplus_free_checked(&h) != 0 &&
                           cplus_alloc_is_live(p) == 1);
                cplus_free(p);
            }
            break;
        }
        default: {
            void *p = cplus_malloc(n == 0u ? 1u : n);
            f100_check(&c, "case4_alloc", p != NULL);
            if (p != NULL) {
                f100_check(&c, "case4_checked_ok",
                           cplus_free_checked(&p) == 0 && p == NULL);
            }
            break;
        }
        }
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    return f100_end(&c);
}
