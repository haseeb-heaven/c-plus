/* F-034: libc cplus_aligned_alloc alignment contract and side-table
 * registration.  This configuration reports a real defect (exit 2) when the
 * returned block is absent from the hardened allocation metadata table. */
#define CPLUS_LIBC_SAFE_DISABLE 1
#include "f100_alloc_harness.h"
#include "cplus_libc.h"

static const size_t f100_alignments[] = {8u, 16u, 32u, 64u, 128u, 256u};
static const size_t f100_bad_alignments[] = {0u, 1u, 3u, 24u, 96u};

int main(void) {
    f100_ctx c;
    f100_lcg r;
    size_t base, ops = 0u;
    unsigned i;
    int defect = 0;

    f100_begin(&c, "F-034");
    f100_seed(&r, F100_SEED ^ 0x034u);
    base = cplus_alloc_live_count();

    /* Focused registration probe.  No stale pointer is dereferenced. */
    {
        cplus_alloc_meta m;
        size_t alignment = 64u, sz = 4096u;
        unsigned char *p = (unsigned char *)cplus_aligned_alloc(alignment, sz);
        f100_check(&c, "aligned_nonnull", p != NULL);
        if (p != NULL) {
            int live = cplus_alloc_is_live(p);
            int gm = cplus_alloc_get_meta(p, &m);
            size_t reported = cplus_alloc_size(p);
            size_t rem = cplus_alloc_remaining_capacity(p);
            printf("F-034 PROBE p=%p alignment_ok=%d live=%d get_meta=%d "
                   "size=%zu remaining=%zu\n",
                   (void *)p, (((uintptr_t)p % alignment) == 0u), live, gm,
                   reported, rem);
            if (!live || gm != 0 || reported != sz || rem != sz) {
                printf("DEFECT F-034 cplus_aligned_alloc returns memory that is "
                       "absent from the hardened allocation side table: "
                       "live=%d get_meta=%d size=%zu remaining=%zu; "
                       "cplus_free() then refutes it as an invalid free and "
                       "leaks the block (libc/src/stdlib/stdlib_c17.c calls "
                       "aligned_alloc() directly instead of the tracked path)\n",
                       live, gm, reported, rem);
                defect = 1;
            }
            cplus_free(p); /* tracked release, like the loop below */
        }
    }

    for (i = 0u; i < F100_OPS; ++i) {
        size_t a, sz;
        unsigned char *p;

        if (i < (unsigned)(sizeof(f100_bad_alignments) /
                           sizeof(f100_bad_alignments[0]))) {
            errno = 0;
            p = (unsigned char *)cplus_aligned_alloc(f100_bad_alignments[i], 64u);
            f100_check(&c, "bad_align_refused", p == NULL && errno == EINVAL);
            ++ops;
            continue;
        }
        a = f100_alignments[f100_next(&r) %
                            (unsigned)(sizeof(f100_alignments) /
                                       sizeof(f100_alignments[0]))];
        sz = a * ((f100_next(&r) % 8u) + 1u); /* size % alignment == 0 */
        p = (unsigned char *)cplus_aligned_alloc(a, sz);
        f100_check(&c, "aligned_nonnull", p != NULL);
        if (p == NULL) break;
        f100_check(&c, "alignment_honored", ((uintptr_t)p % a) == 0u);
        p[0] = (unsigned char)f100_next(&r);
        p[sz - 1u] = p[0];
        /* cplus_aligned_alloc now registers blocks in the hardened side table,
         * so release through cplus_free to keep metadata balanced. */
        cplus_free(p);
        ++ops;
    }
    f100_check(&c, "op_count", ops == F100_OPS);
    f100_check(&c, "live_balance", cplus_alloc_live_count() == base);
    if (defect) {
        printf("F100 F-034 DEFECT_CONFIRMED=1\n");
        return 2; /* distinct status: real defect, not a harness failure */
    }
    return f100_end(&c);
}
