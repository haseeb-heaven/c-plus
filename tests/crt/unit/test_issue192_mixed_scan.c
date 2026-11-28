/* Regression for the OpenQodex fallback review finding: when one conversion
 * in a scanf_s format has an Annex K capacity, a second unbounded %s must
 * NOT be forwarded to host scanf without a field width. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "stdlib/safe/safe_format.h"
#include "stdlib/safe/safe_runtime.h"

int main(void) {
    char a[8] = {0}, b[8] = {0};
    int r;

    /* Refusals must be observable as a return value, not a process trap. */
    (void)cplus_runtime_set_violation_trap(0);

    /* First conversion supplies a capacity, second has neither capacity nor
     * width: the whole call must be refused before any host scan. */
    /* Call through a plain function pointer so the compiler's printf-format
     * attribute does not misread the Annex K capacity argument. */
    {
        typedef int (*scanf3_fn)(const char *, const char *, ...);
        scanf3_fn scan = cplus_sscanf_s;
        errno = 0;
        r = scan("abc def", "%s %s", a, (size_t)sizeof a, b);
    }
    if (r >= 0) {
        fprintf(stderr, "mixed unbounded scanf accepted: r=%d\n", r);
        return 1;
    }
    if (b[0] != '\0') {
        fprintf(stderr, "mixed unbounded scanf wrote into unprotected buffer\n");
        return 1;
    }

    /* POSIX width-bounded scans remain legal without capacities. */
    a[0] = '\0';
    if (cplus_sscanf_s("abc", "%3s", a) != 1 || strcmp(a, "abc") != 0) {
        fprintf(stderr, "width-bounded POSIX scan broken\n");
        return 1;
    }
    puts("issue192_mixed_scan: ok");
    return 0;
}
