#include <stdio.h>
int main(void) {
    volatile long long sink = 0;
    long long acc = 0;
    for (int i = 0; i < 4000; ++i)
        for (int j = 0; j < 4000; ++j)
            acc += (long long)i * j - (long long)j * i + 1;
    sink = acc;
    printf("%lld\n", sink);
    return 0;
}
