/* Minimal genuine C23 program.
 *
 * Each feature was probed first with /usr/bin/clang -std=c2x on this host
 * (Apple clang 16.0.0) and confirmed to compile, and was then confirmed to
 * also pass through the C+ driver frontend:
 *   1. __STDC_VERSION__ >= 202000L - the C23/C2x draft version macro; this
 *      guard makes the program genuinely require C23 mode (C17 reports
 *      201710L and fails the #error).
 *   2. 0b binary integer literals        - C23
 *   3. typeof operator                   - C23
 *   4. #elifdef preprocessor directive   - C23 (warns as a C2x extension in
 *      default mode, clean under -std=c2x)
 *
 * Note: C23 nullptr was probed too and compiles with plain clang -std=c2x,
 * but the C+ driver frontend rejects it ([CPLUS-FFI] error). It is therefore
 * deliberately not used here; see RESULTS.md.
 *
 * Expected output (clang -std=c2x, __STDC_VERSION__ == 202000L):
 *   STDC=202000 mask=10 branch=elifdef-taken
 */
#include <stdio.h>

#ifndef __STDC_VERSION__
#error "C23 __STDC_VERSION__ is required"
#endif
#if __STDC_VERSION__ < 202000L
#error "This program requires C23 (__STDC_VERSION__ >= 202000L)"
#endif

#define LOW_NIBBLE 0b1010 /* C23 binary literal */

int main(void)
{
    typeof(LOW_NIBBLE) mask = LOW_NIBBLE; /* C23 typeof */

#if 0
    const char *branch = "never";
#elifdef __STDC_VERSION__                /* C23 #elifdef */
    const char *branch = "elifdef-taken";
#else
    const char *branch = "else";
#endif

    printf("STDC=%ld mask=%d branch=%s\n",
           (long)__STDC_VERSION__, mask, branch);
    return 0;
}
