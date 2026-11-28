/* F-044: cplus_vsnprintf_s (va_list path) with the fixed spec table, a
 * 0..512 truncation sweep, mixed boundary arguments, and NULL dest/dmax=0.
 * A second call with the same arguments and a larger capacity checks the
 * would-be-length contract. */
#include "f100_fmt_common.h"

int main(void) {
    f100_bbuf b, wide;
    size_t sz;
    unsigned i, vi;
    const f100_spec *sp;
    int rc, rc_wide;

    f100_init();
    for (sz = 0; sz <= 512; ++sz) {
        sp = &f100_specs[f100_below(F100_NSPEC)];
        f100_bbuf_init(&b);
        rc = f100_call_vsn(b.buf, sz, sp, f100_next());
        F100_OP();
        if (rc < 0) F100_FAIL("vsnprintf_s error");
        if (!f100_bbuf_ok(&b)) F100_FAIL("vsnprintf_s wrote past dmax");
        if (sz > 0 && rc >= (int)sz && b.buf[sz - 1] != '\0')
            F100_FAIL("vsnprintf_s truncation missing NUL");
    }
    for (i = 0; i < 1100; ++i) {
        sp = &f100_specs[f100_below(F100_NSPEC)];
        sz = f100_below(513u);
        vi = f100_next();
        f100_bbuf_init(&b);
        f100_bbuf_init(&wide);
        rc = f100_call_vsn(b.buf, sz, sp, vi);
        rc_wide = f100_call_vsn(wide.buf, sizeof wide.buf, sp, vi);
        F100_OP();
        if (rc < 0 || rc_wide < 0) F100_FAIL("vsnprintf_s random error");
        if (rc != rc_wide) F100_FAIL("vsnprintf_s would-be length differs by dmax");
        if (!f100_bbuf_ok(&b) || !f100_bbuf_ok(&wide))
            F100_FAIL("vsnprintf_s random overflow");
    }
    for (i = 0; i < 32; ++i) {
        sp = &f100_specs[f100_below(F100_NSPEC)];
        rc = f100_call_vsn(NULL, 0, sp, f100_next());
        F100_OP();
        if (rc < 0) F100_FAIL("vsnprintf_s NULL/dmax=0 rejected");
    }
    return f100_report("F-044");
}
