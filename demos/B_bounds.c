/* Demo B: an invalid array access must fail safely, never corrupt memory.
 *
 * A constant bad index is rejected at compile time ("array index ... past
 * the end" is an error).  This demo uses a volatile index, so the violation
 * is only visible at run time: safe C+ aborts with a UBSan diagnostic. */
#include <stdio.h>

static volatile int bad_index = 9;

int main(void) {
    int samples[4] = {10, 20, 30, 40};
    printf("valid: %d\n", samples[2]);
    printf("invalid: %d\n", samples[bad_index]);
    printf("this line must not be reached\n");
    return 0;
}
