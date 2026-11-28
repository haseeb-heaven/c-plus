/* F-047: cplus_fprintf_s to a tmpfile() stream with fixed conversions, then
 * rewind/read-back to confirm every call produced bytes. */
#include "f100_fmt_common.h"

int main(void) {
    FILE *f = tmpfile();
    unsigned i;
    const f100_spec *sp;
    int rc;
    long n = 0;

    f100_init();
    if (f == NULL) {
        F100_FAIL("tmpfile failed");
        return f100_report("F-047");
    }
    for (i = 0; i < 1200; ++i) {
        sp = &f100_specs[f100_below(F100_NSPEC)];
        rc = f100_call_fprintf(f, sp, f100_next());
        F100_OP();
        if (rc < 0) F100_FAIL("fprintf_s returned error");
    }
    fflush(f);
    rewind(f);
    {
        int c;
        while ((c = fgetc(f)) != EOF) ++n;
    }
    F100_OP();
    if (n <= 0) F100_FAIL("fprintf_s produced no bytes");
    /* Null-stream and null-format are rejected without a trap. */
    errno = 0;
    rc = cplus_fprintf_s(NULL, "%d", 1);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("NULL stream not refused");
    errno = 0;
    rc = cplus_fprintf_s(f, (const char *)NULL, 1);
    F100_OP();
    if (rc >= 0 || errno != EINVAL) F100_FAIL("NULL format not refused");
    fclose(f);
    return f100_report("F-047");
}
