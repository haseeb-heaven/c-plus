#include <stdio.h>
#include <stdlib.h>
static int cmp_int(const void *pa, const void *pb) {
    int a = *(const int *)pa, b = *(const int *)pb;
    return (a > b) - (a < b);
}
int main(void) {
    int a[6] = {60, 1426, 2269, 3277, 4209, 5240};
    qsort(a, 6, sizeof(a[0]), cmp_int);
    printf("sorted=%d,%d,%d,%d,%d,%d\n", a[0], a[1], a[2], a[3], a[4], a[5]);
    return 0;
}
