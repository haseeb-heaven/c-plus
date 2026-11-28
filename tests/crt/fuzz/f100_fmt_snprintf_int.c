/* F-042: snprintf_s integer boundary values (INT_MIN/MAX, LONG/LLONG extremes,
 * UINT_MAX, ULLONG_MAX, SIZE_MAX) across every integer conversion, with random
 * dmax so both full output and truncation are exercised. */
#include "f100_fmt_common.h"

/* Table indices 0..15 are the integer-kind conversions in f100_specs. */
static const unsigned int_idx[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
#define N_IDX ((unsigned)(sizeof int_idx / sizeof int_idx[0]))

int main(void) {
    f100_bbuf b;
    unsigned i, k, vi;
    size_t dmax;
    int rc;

    f100_init();
    /* Exhaustive boundary sweep: each integer spec x each boundary value. */
    for (k = 0; k < N_IDX; ++k) {
        const f100_spec *sp = &f100_specs[int_idx[k]];
        for (vi = 0; vi < 12; ++vi) {
            for (i = 0; i < 2; ++i) {
                dmax = i ? 3u : 128u;
                f100_bbuf_init(&b);
                rc = f100_call_sn(b.buf, dmax, sp, vi);
                F100_OP();
                if (rc < 0) F100_FAIL("integer boundary format returned error");
                if (!f100_bbuf_ok(&b)) F100_FAIL("integer boundary format overflowed");
            }
        }
    }
    /* Random continuation to exceed 1000 ops. */
    for (i = 0; i < 1100; ++i) {
        const f100_spec *sp = &f100_specs[int_idx[f100_below(N_IDX)]];
        dmax = 1u + f100_below(200u);
        f100_bbuf_init(&b);
        rc = f100_call_sn(b.buf, dmax, sp, f100_next());
        F100_OP();
        if (rc < 0 || !f100_bbuf_ok(&b)) F100_FAIL("random integer format case");
    }
    return f100_report("F-042");
}
