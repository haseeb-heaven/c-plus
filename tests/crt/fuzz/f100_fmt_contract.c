/* F-050: format safety-contract probes mixed with valid bounded calls.
 * Negative cases (%n, non-literal checked format, NULL format, NULL dest with
 * non-zero dmax) use volatile byte buffers so the compiler's format-attribute
 * checker cannot turn the deliberate probe into a build-time diagnostic; the
 * library must itself return -1/EINVAL.  The violation trap is disabled by
 * f100_init() so the return path can be asserted (documented in the notes). */
#include "f100_fmt_common.h"

int main(void) {
    f100_bbuf b;
    volatile char pct_n[] = "%n";
    volatile char plain[] = "%d";
    unsigned i;
    int rc;

    f100_init();
    for (i = 0; i < 1050; ++i) {
        f100_bbuf_init(&b);
        rc = f100_call_sn(b.buf, 1u + f100_below(200u),
                          &f100_specs[f100_below(F100_NSPEC)], f100_next());
        F100_OP();
        if (rc < 0 || !f100_bbuf_ok(&b)) F100_FAIL("valid bounded format case");
    }

    /* Forbidden %n in the snprintf and printf families. */
    errno = 0;
    rc = cplus_snprintf_s(b.buf, sizeof b.buf, (const char *)pct_n);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("%n not refused by snprintf_s");
    errno = 0;
    rc = cplus_printf_s((const char *)pct_n);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("%n not refused by printf_s");

    /* Non-literal format is refused by the checked entry point. */
    errno = 0;
    rc = cplus_printf_s_checked(0, (const char *)plain, 1);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("non-literal printf_s accepted");
    errno = 0;
    rc = cplus_fprintf_s_checked(0, stdout, (const char *)plain, 1);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("non-literal fprintf_s accepted");

    /* NULL format and NULL destination with non-zero capacity. */
    errno = 0;
    rc = cplus_snprintf_s(b.buf, sizeof b.buf, (const char *)NULL);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("NULL format accepted");
    errno = 0;
    rc = cplus_printf_s((const char *)NULL);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("NULL printf format accepted");
    errno = 0;
    rc = cplus_snprintf_s(NULL, 8u, "%d", 1);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("NULL dest with dmax>0 accepted");
    return f100_report("F-050");
}
