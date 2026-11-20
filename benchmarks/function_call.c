/* Benchmark: non-inlined function call overhead. */
#include <stdio.h>

__attribute__((noinline)) static int step(int x) {
    return x * 3 + 1;
}

#define ITERATIONS 100000000

int main(void) {
    int v = 0;
    int i;
    for (i = 0; i < ITERATIONS; i++)
        v = step(v & 1023);
    printf("call %d\n", v);
    return 0;
}
