#include <stdio.h>
#define ITERS 200000000
int main(void) {
    unsigned long long x = 1ULL;
    unsigned long long acc = 0ULL;
    unsigned long long i;
    for (i = 0; i < ITERS; ++i) {
        x = x * 1103515245ULL + 12345ULL;
        acc += (x >> 16) & 0xffULL;
    }
    printf("arith=%llu\n", acc);
    return 0;
}
