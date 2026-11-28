#include <stdio.h>
int main(void) {
    int a[5] = {388, 563, 619, 414, 812};
    int *p = a;
    printf("p0=%d p2=%d p4=%d\n", *p, *(p + 2), *(p + 4));
    p++;
    printf("adv=%d diff=%ld\n", *p, (long)(p - a));
    return 0;
}
