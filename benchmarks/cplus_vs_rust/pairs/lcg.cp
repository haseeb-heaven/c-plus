/* Paired benchmark: raw 64-bit LCG recurrence.
 *
 * The multiply-add recurrence cannot be reduced to a closed form, so no
 * optimizer folds the loop away.  Unsigned wrap is defined in C and Rust,
 * which keeps stdout identical across all four builds. */
#include <stdio.h>

#define ITERATIONS 200000000LL

int main(void) {
    volatile unsigned long long seed = 1ULL; /* stops constant folding */
    unsigned long long x = seed;
    long long i;

    for (i = 0; i < ITERATIONS; i++)
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;

    printf("lcg %llu\n", x);
    return 0;
}
