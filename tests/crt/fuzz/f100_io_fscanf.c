/* F-059: cplus_fscanf_s over a deterministic temp-file fixture.  1200 integer
 * records are written with fprintf, read back with "%d" through the checked
 * scanner, and compared to the generated values; a second pass reads a fixed
 * set of float records with "%lf". */
#include "f100_io_common.h"

int main(void) {
    char path[512];
    FILE *f;
    long expected[1200];
    double dexp[8];
    unsigned i;
    int rc, got;
    double dg;

    f100_init();
    f100_tmp_path(path, sizeof path, "fscanf");
    f = cplus_fopen_s(path, "wb+");
    if (f == NULL) { F100_FAIL("fopen_s failed"); return f100_report("F-059"); }

    for (i = 0; i < 1200; ++i) {
        expected[i] = (long)(f100_next() % 200000UL) - 100000L;
        fprintf(f, "%ld\n", expected[i]);
    }
    {
        static const char *flt[] = { "0.0", "1.5", "-2.25", "3.125",
                                     "1e3", "-4.5e-2", "7.75", "0.001" };
        for (i = 0; i < 8; ++i) {
            dexp[i] = strtod(flt[i], NULL);
            fprintf(f, "%s\n", flt[i]);
        }
    }
    fflush(f);
    rewind(f);

    for (i = 0; i < 1200; ++i) {
        got = -999;
        rc = cplus_fscanf_s(f, "%d", &got);
        F100_OP();
        if (rc != 1 || (long)got != expected[i])
            F100_FAIL("fscanf_s integer record mismatch");
    }
    for (i = 0; i < 8; ++i) {
        dg = 0.0;
        rc = cplus_fscanf_s(f, "%lf", &dg);
        F100_OP();
        if (rc != 1 || dg != dexp[i]) F100_FAIL("fscanf_s float record mismatch");
    }
    /* EOF is a normal negative result, not a violation. */
    dg = 0.0;
    rc = cplus_fscanf_s(f, "%lf", &dg);
    F100_OP();
    if (rc >= 0) F100_FAIL("fscanf_s did not report EOF");
    if (cplus_fclose_s(f) != 0) F100_FAIL("final close failed");
    (void)unlink(path);
    return f100_report("F-059");
}
