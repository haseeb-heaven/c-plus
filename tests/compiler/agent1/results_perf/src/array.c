#include <stdio.h>
#define N 4096
#define REPS 150000
int main(void) {
    int a[N];
    int i, r;
    long long s = 0;
    for (i = 0; i < N; ++i) a[i] = i & 63;
    for (r = 0; r < REPS; ++r)
        for (i = 0; i < N; ++i) s += a[i];
    printf("array_sum=%lld\n", s);
    return 0;
}
