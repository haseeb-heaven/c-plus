/* F-051: cplus_fopen_s / cplus_fclose_s lifecycle under repeated churn.
 * Each cycle opens a temp file, writes one byte, closes, and probes the
 * double-close refusal path.  Argument-validation probes use NULL name/mode. */
#include "f100_io_common.h"

int main(void) {
    char path[512];
    unsigned i;
    FILE *f;
    int rc;

    f100_init();
    f100_tmp_path(path, sizeof path, "openclose");
    for (i = 0; i < 350; ++i) {
        f = cplus_fopen_s(path, "wb+");
        F100_OP();
        if (f == NULL) { F100_FAIL("fopen_s failed in churn loop"); break; }
        if (cplus_fwrite_s("x", 1, 1, 1, f) != 1)
            F100_FAIL("fwrite_s failed in churn loop");
        F100_OP();
        rc = cplus_fclose_s(f);
        F100_OP();
        if (rc != 0) F100_FAIL("fclose_s failed on live stream");
        errno = 0;
        rc = cplus_fclose_s(f);
        F100_OP();
        if (rc != EOF || errno != EINVAL)
            F100_FAIL("double close was not refused");
    }
    errno = 0;
    if (cplus_fopen_s(NULL, "wb") != NULL || errno != EINVAL)
        F100_FAIL("NULL name accepted");
    F100_OP();
    errno = 0;
    if (cplus_fopen_s(path, NULL) != NULL || errno != EINVAL)
        F100_FAIL("NULL mode accepted");
    F100_OP();
    rc = cplus_fclose_s(NULL);
    F100_OP();
    if (rc != EOF) F100_FAIL("NULL stream close did not return EOF");
    (void)unlink(path);
    return f100_report("F-051");
}
