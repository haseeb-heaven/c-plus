/* F-028: deterministic allocation-failure injection at fixed sequence
 * points; sequencing is observed with cplus_safe_alloc_seq(). */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-028");
    f100_seed(&r, F100_SEED ^ 0x028u);
    base = cplus_safe_alloc_live_count();
    cplus_safe_alloc_fail_reset();
    f100_check(&c, "seq_reset", cplus_safe_alloc_seq() == 0UL);

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        void *p;

        if (i % 101u == 0u) {
            void *a, *b, *d;
            unsigned long s0, s1, s2, s3, s4;
            cplus_safe_alloc_fail_reset();
            s0 = cplus_safe_alloc_seq();
            cplus_safe_alloc_fail_after(2);
            a = malloc_s(8u); s1 = cplus_safe_alloc_seq();
            b = malloc_s(8u); s2 = cplus_safe_alloc_seq();
            errno = 0;
            d = malloc_s(8u); s3 = cplus_safe_alloc_seq();
            f100_check(&c, "phase_success_1", a != NULL && s1 == s0 + 1u);
            f100_check(&c, "phase_success_2", b != NULL && s2 == s0 + 2u);
            f100_check(&c, "phase_fail", d == NULL && errno == ENOMEM &&
                                          s3 == s0 + 3u);
            f100_check(&c, "phase_fail_again", malloc_s(8u) == NULL);
            s4 = cplus_safe_alloc_seq();
            f100_check(&c, "phase_seq_advances", s4 == s0 + 4u);
            free_s(a); free_s(b);
            cplus_safe_alloc_fail_reset();
            f100_check(&c, "phase_seq_reset", cplus_safe_alloc_seq() == 0u);
        }

        p = malloc_s(n == 0u ? 1u : n);
        f100_check(&c, "normal_alloc", p != NULL);
        if (p == NULL) break;
        free_s(p);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
