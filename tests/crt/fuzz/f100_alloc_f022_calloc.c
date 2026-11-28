/* F-022: calloc_s deterministic count/size products including values at and
 * beyond the overflow edge, plus zero-count/zero-size normalization. */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-022");
    f100_seed(&r, F100_SEED ^ 0x022u);
    base = cplus_safe_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t count, elem;
        unsigned char *p;

        if (i < (unsigned)F100_OVERFLOW_N) {
            count = f100_overflow_products[i];
            elem = 2u; /* doubles every product above */
        } else {
            count = f100_size(&r) / 16u;
            elem = (f100_next(&r) % 8u) + 1u;
        }
        errno = 0;
        p = (unsigned char *)calloc_s(count, elem);

        if (count == 0u || elem == 0u) {
            f100_check(&c, "zero_normalized",
                       p != NULL && cplus_safe_ptr_size(p) == 1u &&
                       p[0] == 0u);
            free_s(p);
            ++ops;
            continue;
        }
        if (count > SIZE_MAX / elem || count * elem > F100_RSIZE_MAX) {
            f100_check(&c, "overflow_refused", p == NULL && errno == EOVERFLOW);
            free_s(p);
            ++ops;
            continue;
        }
        f100_check(&c, "calloc_nonnull", p != NULL);
        if (p == NULL) break;
        f100_check(&c, "byte_size", cplus_safe_ptr_size(p) == count * elem);
        f100_check(&c, "elem_size", cplus_safe_ptr_elem_size(p) == elem);
        f100_check(&c, "elem_count", cplus_safe_ptr_count(p) == count);
        if (p[0] != 0u || p[count * elem - 1u] != 0u)
            printf("FAIL %s zero_fill i=%u\n", c.tag, i), c.fails++, c.checks++;
        else
            c.checks++;
        free_s(p);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
