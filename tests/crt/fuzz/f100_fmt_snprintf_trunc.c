/* F-041: snprintf_s truncation boundary sweep, sizes 0..512 (x3), plus the
 * legal NULL-destination / dmax==0 measurement case. */
#include "f100_fmt_common.h"

int main(void) {
    f100_bbuf b;
    size_t sz;
    unsigned pass;
    const f100_spec *sp;
    int rc;

    f100_init();
    for (pass = 0; pass < 3; ++pass) {
        for (sz = 0; sz <= 512; ++sz) {
            sp = &f100_specs[f100_below(F100_NSPEC)];
            f100_bbuf_init(&b);
            rc = f100_call_sn(b.buf, sz, sp, f100_next());
            F100_OP();
            if (rc < 0) F100_FAIL("snprintf_s error for bounded call");
            if (!f100_bbuf_ok(&b)) F100_FAIL("snprintf_s wrote past dmax");
            if (sz > 0 && rc >= (int)sz && b.buf[sz - 1] != '\0')
                F100_FAIL("truncated output not NUL-terminated at dmax-1");
        }
    }
    for (pass = 0; pass < 32; ++pass) {
        sp = &f100_specs[f100_below(F100_NSPEC)];
        rc = f100_call_sn(NULL, 0, sp, f100_next());
        F100_OP();
        if (rc < 0) F100_FAIL("NULL dest with dmax=0 must return would-be length");
    }
    return f100_report("F-041");
}
