#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/benchmarks/bounds_sum.c"
/* Benchmark: fixed-extent array access with a dynamic index.
 *
 * The index is masked so the compiler cannot prove it in range, which forces
 * the safe build to keep its bounds check.  This measures what memory safety
 * costs on the hot path, which is the number people actually care about.
 */
#include <stdio.h>

#define SLOTS 16
#define ITERATIONS 60000000

static volatile int sink;

int main(void) {
    int values[SLOTS];
    long long total = 0;
    int i;

    for (i = 0; i < SLOTS; i++)
        values[i] = i * 3;

    for (i = 0; i < ITERATIONS; i++)
        total += values[i & (SLOTS - 1)];

    sink = (int)(total & 0x7fffffff);
    printf("bounds_sum %d\n", sink);
    return 0;
}
