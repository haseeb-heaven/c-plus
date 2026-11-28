/* F-039: libc allocation tracked-size boundary against mmap guard pages in
 * forked children, plus a 1200-op deterministic sweep. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

static int f100_libc_copyback(unsigned char *dst, size_t n) {
    unsigned char *p = (unsigned char *)cplus_malloc(n);
    size_t reported;
    if (p == NULL) return 1;
    memset(p, 0xA5, n);
    reported = cplus_alloc_size(p);
    if (reported != n) { cplus_free(p); return 1; }
    memcpy(dst, p, reported);
    cplus_free(p);
    return 0;
}

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;
    int canary_rc;

    f100_begin(&c, "F-039");
    f100_seed(&r, F100_SEED ^ 0x039u);
    base = cplus_alloc_live_count();

    for (i = 0u; i < F100_OPS; ++i) {
        cplus_alloc_meta m;
        unsigned char *p = (unsigned char *)cplus_malloc(f100_size(&r));
        f100_check(&c, "alloc_nonnull", p != NULL);
        if (p == NULL) break;
        f100_check(&c, "meta_ok",
                   cplus_alloc_get_meta(p, &m) == 0 &&
                   m.byte_size == cplus_alloc_size(p));
        cplus_free(p);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);

    canary_rc = f100_canary_check("F-039", f100_libc_copyback);
    f100_check(&c, "canary_pages", canary_rc == 0);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    return f100_end(&c);
}
