/* F-046: cplus_printf_s with fixed literal conversions.  stdout is dup2'd to
 * a temp file for the run and restored before reporting so the CONFIG line is
 * not swallowed by the redirect. */
#include <unistd.h>
#include "f100_fmt_common.h"

int main(void) {
    char path[512];
    int saved;
    FILE *sink;
    unsigned i;
    const f100_spec *sp;
    int rc;

    f100_init();
    f100_tmp_path(path, sizeof path, "printf");
    saved = dup(STDOUT_FILENO);
    sink = fopen(path, "wb");
    if (saved < 0 || sink == NULL) {
        F100_FAIL("cannot set up stdout sink");
        if (saved >= 0) close(saved);
        return f100_report("F-046");
    }
    if (dup2(fileno(sink), STDOUT_FILENO) < 0) {
        F100_FAIL("dup2 failed");
    } else {
        for (i = 0; i < 1200; ++i) {
            sp = &f100_specs[f100_below(F100_NSPEC)];
            rc = f100_call_printf(sp, f100_next());
            F100_OP();
            if (rc < 0) F100_FAIL("printf_s returned error");
        }
        fflush(stdout);
        if (dup2(saved, STDOUT_FILENO) < 0) F100_FAIL("stdout restore failed");
    }
    close(saved);
    fclose(sink);

    /* The sink must contain bytes; verify without depending on its content. */
    {
        FILE *in = fopen(path, "rb");
        long n = 0;
        if (in == NULL) {
            F100_FAIL("cannot reopen printf sink");
        } else {
            int c;
            while ((c = fgetc(in)) != EOF) ++n;
            fclose(in);
            if (n <= 0) F100_FAIL("printf_s produced no output");
        }
    }
    (void)unlink(path);
    return f100_report("F-046");
}
