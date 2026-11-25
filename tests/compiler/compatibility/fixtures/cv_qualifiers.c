/* const/volatile and the gnu89 spelling of restrict (__restrict). */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

static int sum(const int *__restrict a, int n) {
    int i, t = 0;
    for (i = 0; i < n; ++i)
        t += a[i];
    return t;
}

int main(void) {
    const int arr[3] = { 2, 4, 6 };
    volatile int v = 9;
    v = v + 1;

    printf("%d %d\n", sum(arr, 3), v);
    printf("%d %d\n", (int)(sizeof(arr) == 3 * sizeof(int)),
                      (int)(sizeof(v) == sizeof(int)));
    return 0;
}
