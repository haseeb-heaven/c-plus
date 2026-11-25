/* Bug 614 (FP comparison with NaN): a NaN is never equal to anything,
   so this direct equality test silently takes the wrong branch. */
#include <math.h>
#include <stdio.h>

int main(void) {
    double v = 0.0;
    if (v == NAN) printf("equal\n");
    return 0;
}
