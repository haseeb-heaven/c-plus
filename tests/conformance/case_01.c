/* Gate 01: constant array index out of bounds (function-pointer table, index 10 of length 3).
 * Expected outcome class in this runner: runtime_reject (raw SIGSEGV under plain clang).
 * C+ note: `c+ check` DOES reject this statically with [CPLUS-BOUNDS], but the
 * conformance runner compiles with plain clang and does not run the C+ static
 * checker. UBSan (not C+) would report "index 10 out of bounds" at runtime if
 * it were enabled; here it is not, so the only signal is the crash.
 */
#include <stdio.h>

typedef int (*fp)(void);

static int one(void)   { return 1; }
static int two(void)   { return 2; }
static int three(void) { return 3; }

int main(void) {
    fp table[3] = { one, two, three };
    fp target = table[10];              /* constant index, length is 3 */
    printf("gate01: constant index 10 into length-3 function-pointer table\n");
    fflush(stdout);
    target();                            /* indirect call through the OOB slot */
    return 0;
}
