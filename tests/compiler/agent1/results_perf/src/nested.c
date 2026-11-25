#include <stdio.h>
#define R 100000
#define C 10000
int main(void) {
    int i, j;
    long long s = 0;
    for (i = 0; i < R; ++i)
        for (j = 0; j < C; ++j)
            s += (long long)((i & 15) * (j & 15));
    printf("nested=%lld\n", s);
    return 0;
}
