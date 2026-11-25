/* F-048: checked entry points cplus_snprintf_s_checked /
 * cplus_sprintf_s_checked with fmt_is_literal=1 and a matching known_dmax,
 * plus their documented refusal paths (non-literal format, dmax > capacity). */
#include "f100_fmt_common.h"

int main(void) {
    f100_bbuf b;
    unsigned i, vi;
    size_t dmax;
    int rc;

    f100_init();
    for (i = 0; i < 1200; ++i) {
        vi = f100_next();
        dmax = 1u + f100_below(240u);
        f100_bbuf_init(&b);
        if (i & 1u) {
            rc = cplus_snprintf_s_checked(1, sizeof b.buf, b.buf, dmax,
                    "%s/%d/%.3f/%llx",
                    f100_str_vals[vi % F100_NSTR],
                    f100_int_vals[(vi >> 3) % F100_NINT],
                    f100_dbl_vals[(vi >> 7) % F100_NDBL],
                    f100_ull_vals[(vi >> 11) % F100_NULLV]);
        } else {
            rc = cplus_sprintf_s_checked(1, sizeof b.buf, b.buf, dmax,
                    "%s/%u/%lld/%zu",
                    f100_str_vals[vi % F100_NSTR],
                    f100_uint_vals[(vi >> 3) % F100_NUINT],
                    f100_ll_vals[(vi >> 7) % F100_NLL],
                    f100_size_vals[(vi >> 11) % F100_NSIZE]);
        }
        F100_OP();
        if (rc < 0) F100_FAIL("checked format returned error for literal fmt");
        if (!f100_bbuf_ok(&b)) F100_FAIL("checked format wrote past dmax");
    }

    /* Non-literal format must be refused even when the bytes are harmless. */
    {
        volatile char nf[] = "%d";
        f100_bbuf_init(&b);
        errno = 0;
        rc = cplus_snprintf_s_checked(0, sizeof b.buf, b.buf, sizeof b.buf,
                                      (const char *)nf, 1);
        F100_OP();
        if (rc >= 0 || errno != EINVAL) F100_FAIL("non-literal format was accepted");
    }
    /* dmax greater than the caller-declared capacity must be refused. */
    {
        f100_bbuf_init(&b);
        errno = 0;
        rc = cplus_snprintf_s_checked(1, 8u, b.buf, 16u, "%d", 1);
        F100_OP();
        if (rc >= 0 || errno != EINVAL) F100_FAIL("oversized dmax was accepted");
    }
    return f100_report("F-048");
}
