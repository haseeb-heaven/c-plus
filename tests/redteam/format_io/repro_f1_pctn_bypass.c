/*
 * FINDING F1 -- %n detector bypass via repeated positional specifiers.
 *
 * Contract (stdlib/safe/safe_format.h): "%n (including length-modified forms)
 * is refused in all format and scan functions."
 *
 * cplus_fmt_has_percent_n() consumes the first "N$" as a positional index,
 * then the second "M$" as ordinary width digits, and finally advances one byte
 * past the '$' -- landing *after* the 'n'. It never recognises a %n conversion.
 * libc, however, parses the whole "%1$2$n" as a genuine %n write.
 *
 * NOTE: the format is taken from a table of read-only string literals (as in
 * the main harness). Apple libc's own non-literal-format %n mitigation aborts
 * on heap/stack formats, so this reproducer deliberately uses static literals
 * to isolate the SafeCRT checker's failure.
 *
 * Build + run (from repo root):
 *   /usr/bin/clang -std=c99 -O1 -w -Istdlib/safe -Ilibc/install-include \
 *     -Ithird_party/safeclib/include \
 *     tests/redteam/format_io/repro_f1_pctn_bypass.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o /tmp/repro_f1 && /tmp/repro_f1
 *
 * Expected (vulnerable): rc >= 0, sink changed to 4.
 * Expected (fixed):      rc == -1, errno == EINVAL, sink unchanged.
 */
#include <stdio.h>
#include <errno.h>
#include "safe_format.h"
#include "safe_runtime.h"

static const char *const fmts[] = {
    "ABCD%1$2$n", "%1$2$n", "%2$1$n", "%1$2$hn"
};

int main(void) {
    volatile int sink = 0x24681357;
    const char *fmt = fmts[0];
    cplus_runtime_set_violation_trap(0); /* refusals become -1/EINVAL */
    errno = 0;
    int rc = cplus_printf_s(fmt, (int *)&sink, (int *)&sink,
                            (int *)&sink, (int *)&sink);
    printf("fmt=%s rc=%d errno=%d sink=0x%08x (0x24681357 == no write)\n",
           fmt, rc, errno, (unsigned)sink);
    return 0;
}
