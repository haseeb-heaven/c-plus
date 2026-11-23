#include <stdio.h>
int main(void) {
    int a[6] = {59, 19, 100, 87, 92, 53};
    int sum = 0, mn = a[0], mx = a[0];
    for (int i = 0; i < 6; i++) {
        sum += a[i];
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }
    printf("sum=%d min=%d max=%d\n", sum, mn, mx);
    return 0;
}
