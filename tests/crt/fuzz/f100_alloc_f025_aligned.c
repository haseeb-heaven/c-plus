/* F-025: aligned_alloc_s with power-of-two alignments and deterministic
 * sizes, including invalid-alignment refusal. */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

static const size_t f100_alignments[] = {8u, 16u, 32u, 64u, 128u, 256u};
static const size_t f100_bad_alignments[] = {0u, 1u, 3u, 24u, 96u};

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-025");
    f100_seed(&r, F100_SEED ^ 0x025u);
    base = cplus_safe_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n, a;
        unsigned char *p;

        if (i < (unsigned)(sizeof(f100_bad_alignments) /
                           sizeof(f100_bad_alignments[0]))) {
            errno = 0;
            p = (unsigned char *)aligned_alloc_s(f100_bad_alignments[i], 64u);
            f100_check(&c, "bad_align_refused", p == NULL && errno == EINVAL);
            ++ops;
            continue;
        }
        n = f100_size(&r);
        a = f100_alignments[f100_next(&r) %
                            (unsigned)(sizeof(f100_alignments) /
                                       sizeof(f100_alignments[0]))];
        p = (unsigned char *)aligned_alloc_s(a, n);
        f100_check(&c, "aligned_nonnull", p != NULL);
        if (p == NULL) break;
        f100_check(&c, "alignment_honored", ((uintptr_t)p % a) == 0u);
        f100_check(&c, "size_normalized",
                   cplus_safe_ptr_size(p) == (n == 0u ? 1u : n));
        p[0] = (unsigned char)f100_next(&r);
        free_s(p);
        f100_check(&c, "aligned_released", cplus_safe_ptr_is_live(p) == 0);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
