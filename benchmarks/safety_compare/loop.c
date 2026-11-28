#include <stdio.h>
int main(void) {
    volatile long long sink = 0;
    long long sum = 0;
    for (long long i = 0; i < 200000000LL; ++i) sum += i;
    sink = sum;
    printf("%lld\n", sink);
    return 0;
}
