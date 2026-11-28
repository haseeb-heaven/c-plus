#include <stdio.h>
#include <stdlib.h>
#define N 4096
#define REPS 400000
int main(void) {
    int *a = (int *)malloc((size_t)N * sizeof(int));
    int i, r;
    long long s = 0;
    if (!a) return 2;
    for (i = 0; i < N; ++i) a[i] = i & 63;
    for (r = 0; r < REPS; ++r)
        for (i = 0; i < N; ++i) s += a[i];
    free(a);
    printf("alloc=%lld\n", s);
    return 0;
}
