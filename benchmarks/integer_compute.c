/* Benchmark: unsigned integer arithmetic in a tight loop.  Unsigned wrap is
 * defined in C, so both builds measure the same work without UB checks
 * changing the result. */
#include <stdio.h>

#define ITERATIONS 100000000

static volatile unsigned long long sink;

int main(void) {
    unsigned long long x = 1;
    int i;
    for (i = 0; i < ITERATIONS; i++)
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    sink = x;
    printf("integer %llu\n", sink);
    return 0;
}
