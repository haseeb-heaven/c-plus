/* Demo B: an invalid array access must fail safely, never corrupt memory.
 *
 * Safe C+ compiles with UBSan bounds/null/alignment checks and aborts on the
 * first violation.  Running this demo exits with a runtime diagnostic. */
#include <stdio.h>

int main(void) {
    int samples[4] = {10, 20, 30, 40};
    printf("valid: %d\n", samples[2]);
    printf("invalid: %d\n", samples[9]);
    printf("this line must not be reached\n");
    return 0;
}
