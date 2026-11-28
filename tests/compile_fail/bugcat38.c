/* Bug 645 (FP comparison with NaN): comparing against NaN with != is
   always true, so the guard never detects an invalid result. */
#include <math.h>
#include <stdio.h>

int main(void) {
    double v = 0.0;
    if (NAN != v) printf("different\n");
    return 0;
}
