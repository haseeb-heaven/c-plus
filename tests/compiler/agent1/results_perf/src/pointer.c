#include <stdio.h>
#define N 4096
#define REPS 150000
int main(void) {
    int a[N];
    int *p = a;
    int *q;
    int i, r;
    long long s = 0;
    for (i = 0; i < N; ++i) *p++ = i & 63;
    for (r = 0; r < REPS; ++r) {
        q = a;
        for (i = 0; i < N; ++i) s += *q++;
    }
    printf("ptr_sum=%lld\n", s);
    return 0;
}
