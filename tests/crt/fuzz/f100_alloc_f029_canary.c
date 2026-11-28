/* F-029: SafeCRT allocation size boundary exercised against mmap guard
 * pages in forked children, plus a 1200-op deterministic sweep. */
#include "f100_alloc_harness.h"
#include "safe_alloc.h"

static int f100_safe_copyback(unsigned char *dst, size_t n) {
    unsigned char *p = (unsigned char *)malloc_s(n);
    size_t reported;
    if (p == NULL) return 1;
    memset(p, 0xA5, n);
    reported = cplus_safe_ptr_size(p);
    if (reported != n) { free_s(p); return 1; }
    memcpy(dst, p, reported);
    free_s(p);
    return 0;
}

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;
    int canary_rc;

    f100_begin(&c, "F-029");
    f100_seed(&r, F100_SEED ^ 0x029u);
    base = cplus_safe_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        cplus_safe_alloc_meta m;
        unsigned char *p = (unsigned char *)malloc_s(f100_size(&r));
        f100_check(&c, "alloc_nonnull", p != NULL);
        if (p == NULL) break;
        f100_check(&c, "meta_ok",
                   cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK &&
                   m.byte_size == cplus_safe_ptr_size(p));
        free_s(p);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);

    canary_rc = f100_canary_check("F-029", f100_safe_copyback);
    f100_check(&c, "canary_pages", canary_rc == 0);
    f100_check(&c, "live_balance", cplus_safe_alloc_live_count() == base);
    return f100_end(&c);
}
