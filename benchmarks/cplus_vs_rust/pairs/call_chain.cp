/* Paired benchmark: non-inlined function call overhead.
 *
 * The LCG step lives in a noinline function so the loop measures call cost as
 * well as the arithmetic.  The volatile seed prevents constant folding. */
#include <stdio.h>

#define ITERATIONS 200000000LL

__attribute__((noinline)) static unsigned long long step(unsigned long long x) {
    return x * 6364136223846793005ULL + 1442695040888963407ULL;
}

int main(void) {
    volatile unsigned long long seed = 1ULL;
    unsigned long long x = seed;
    long long i;

    for (i = 0; i < ITERATIONS; i++)
        x = step(x);

    printf("call_chain %llu\n", x);
    return 0;
}
