/* F-031: libc cplus_calloc count*size overflow edge and zero normalization. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-031");
    f100_seed(&r, F100_SEED ^ 0x031u);
    base = cplus_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t count, elem, want;
        unsigned char *p;
        cplus_alloc_meta m;

        if (i < (unsigned)F100_OVERFLOW_N) {
            count = f100_overflow_products[i];
            elem = 2u;
        } else {
            count = f100_size(&r) / 16u;
            elem = (f100_next(&r) % 8u) + 1u;
        }
        errno = 0;
        p = (unsigned char *)cplus_calloc(count, elem);

        if (count == 0u || elem == 0u) {
            f100_check(&c, "zero_normalized",
                       p != NULL && cplus_alloc_size(p) == 1u && p[0] == 0u);
            cplus_free(p);
            ++ops;
            continue;
        }
        if (count > SIZE_MAX / elem || count * elem > F100_RSIZE_MAX) {
            /* libc layer reports ENOMEM (not EOVERFLOW) for this edge. */
            f100_check(&c, "overflow_refused", p == NULL && errno == ENOMEM);
            cplus_free(p);
            ++ops;
            continue;
        }
        f100_check(&c, "calloc_nonnull", p != NULL);
        if (p == NULL) break;
        want = count * elem;
        f100_check(&c, "byte_size", cplus_alloc_size(p) == want);
        f100_check(&c, "meta_ok",
                   cplus_alloc_get_meta(p, &m) == 0 && m.is_live == 1 &&
                   m.byte_size == want && m.elem_size == elem &&
                   m.elem_count == count);
        if (p[0] != 0u || p[want - 1u] != 0u) {
            printf("FAIL %s zero_fill i=%u\n", c.tag, i);
            c.fails++;
        }
        c.checks++;
        cplus_free(p);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    return f100_end(&c);
}
