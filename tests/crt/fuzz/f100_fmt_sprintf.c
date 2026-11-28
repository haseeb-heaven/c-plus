/* F-045: cplus_sprintf_s and cplus_vsprintf_s (bounded alias of vsnprintf),
 * dmax 0..160.  dmax==0 is a legal measurement call that must not write; the
 * guard canaries verify that no boundary is crossed. */
#include "f100_fmt_common.h"

int main(void) {
    f100_bbuf b;
    unsigned i;
    size_t dmax;
    const f100_spec *sp;
    int rc;

    f100_init();
    for (i = 0; i < 1300; ++i) {
        sp = &f100_specs[f100_below(F100_NSPEC)];
        dmax = (i < 161) ? (size_t)i : f100_below(161u);
        f100_bbuf_init(&b);
        rc = (i & 1u) ? f100_call_sn(b.buf, dmax, sp, f100_next())
                      : f100_call_vsp(b.buf, dmax, sp, f100_next());
        F100_OP();
        if (rc < 0) F100_FAIL("sprintf_s/vsprintf_s returned error");
        if (!f100_bbuf_ok(&b)) F100_FAIL("sprintf_s/vsprintf_s wrote past dmax");
        if (dmax == 0) {
            size_t j;
            for (j = 0; j < sizeof b.buf; ++j)
                if ((unsigned char)b.buf[j] != F100_CANARY) {
                    F100_FAIL("dmax=0 wrote into destination");
                    break;
                }
        }
    }
    return f100_report("F-045");
}
