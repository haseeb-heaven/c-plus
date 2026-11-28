/* F-043: snprintf_s floating boundary values (0, -0, +/-1, 1e308, DBL_MIN,
 * DBL_MAX, DBL_EPSILON) across %f/%e/%g/%a conversions with random dmax. */
#include "f100_fmt_common.h"

static const unsigned dbl_idx[] = { 16, 17, 18, 19, 20, 21 };
#define N_IDX ((unsigned)(sizeof dbl_idx / sizeof dbl_idx[0]))

int main(void) {
    f100_bbuf b;
    unsigned i, k, vi;
    size_t dmax;
    int rc;

    f100_init();
    for (k = 0; k < N_IDX; ++k) {
        const f100_spec *sp = &f100_specs[dbl_idx[k]];
        for (vi = 0; vi < F100_NDBL; ++vi) {
            for (i = 0; i < 2; ++i) {
                dmax = i ? 4u : 160u;
                f100_bbuf_init(&b);
                rc = f100_call_sn(b.buf, dmax, sp, vi);
                F100_OP();
                if (rc < 0) F100_FAIL("float boundary format returned error");
                if (!f100_bbuf_ok(&b)) F100_FAIL("float boundary format overflowed");
            }
        }
    }
    for (i = 0; i < 1100; ++i) {
        const f100_spec *sp = &f100_specs[dbl_idx[f100_below(N_IDX)]];
        dmax = 1u + f100_below(256u);
        f100_bbuf_init(&b);
        rc = f100_call_sn(b.buf, dmax, sp, f100_next());
        F100_OP();
        if (rc < 0 || !f100_bbuf_ok(&b)) F100_FAIL("random float format case");
    }
    return f100_report("F-043");
}
