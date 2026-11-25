/* F-040: custom allocator plug-in point is not exposed by either allocation
 * API; this is an explicit SKIP.  The configuration still runs a full
 * deterministic mixed-API sweep so the surrounding lifecycle paths are
 * exercised. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"
#include "safe_alloc.h"

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t safe_base, libc_base, ops = 0u;
    unsigned i;

    f100_begin(&c, "F-040");
    f100_seed(&r, F100_SEED ^ 0x040u);
    safe_base = cplus_safe_alloc_live_count();
    libc_base = cplus_alloc_live_count();

    printf("SKIP F-040 custom allocator: no custom allocator plug-in point is "
           "exposed by safe_alloc.h or cplus_libc.h (allocation/lifecycle "
           "APIs are fixed to the backend allocator); covered instead by "
           "deterministic mixed-API lifecycle ops\n");

    for (i = 0u; i < F100_OPS; ++i) {
        size_t n = f100_size(&r);
        void *sp = malloc_s(n);
        void *lp = cplus_malloc(n);
        f100_check(&c, "both_alloc", sp != NULL && lp != NULL);
        if (sp == NULL || lp == NULL) break;
        f100_check(&c, "sizes",
                   cplus_safe_ptr_size(sp) == (n == 0u ? 1u : n) &&
                   cplus_alloc_size(lp) == (n == 0u ? 1u : n));
        free_s(sp);
        cplus_free(lp);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "safe_live_balance",
               cplus_safe_alloc_live_count() == safe_base);
    f100_check(&c, "libc_live_balance",
               cplus_alloc_live_count() == libc_base);
    {
        int rc = f100_end(&c);
        printf("F100 F-040 SKIPPED_CUSTOM_ALLOC=1\n");
        return rc;
    }
}
