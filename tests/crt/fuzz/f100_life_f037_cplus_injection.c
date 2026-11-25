/* F-037: libc deterministic allocation-failure injection at fixed sequence
 * points.  The libc layer has no public sequence counter, so the harness
 * counts every allocation attempt explicitly. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned long attempted = 0u;
    unsigned i;

    f100_begin(&c, "F-037");
    f100_seed(&r, F100_SEED ^ 0x037u);
    base = cplus_alloc_live_count();
    cplus_alloc_fail_reset();

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        void *p;

        if (i % 101u == 0u) {
            void *a, *b, *d;
            cplus_alloc_fail_reset();
            cplus_alloc_fail_after(2);
            a = cplus_malloc(8u); ++attempted;
            b = cplus_malloc(8u); ++attempted;
            errno = 0;
            d = cplus_malloc(8u); ++attempted;
            f100_check(&c, "phase_success_1", a != NULL);
            f100_check(&c, "phase_success_2", b != NULL);
            f100_check(&c, "phase_fail", d == NULL && errno == ENOMEM);
            f100_check(&c, "phase_fail_again", cplus_malloc(8u) == NULL);
            ++attempted;
            cplus_free(a);
            cplus_free(b);
            cplus_alloc_fail_reset();
            {
                void *recovered = cplus_malloc(8u);
                ++attempted;
                f100_check(&c, "phase_recovers", recovered != NULL);
                cplus_free(recovered);
            }
            cplus_free(NULL);
        }

        p = cplus_malloc(n == 0u ? 1u : n);
        ++attempted;
        f100_check(&c, "normal_alloc", p != NULL);
        if (p == NULL) break;
        cplus_free(p);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "attempts_counted", attempted >= F100_OPS);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    return f100_end(&c);
}
