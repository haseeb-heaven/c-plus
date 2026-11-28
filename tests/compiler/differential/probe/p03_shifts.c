#include <stdio.h>

int main(void) {
    unsigned int u = 1u << 31;
    printf("u=%u\n", u);
    printf("shr=%u\n", u >> 31);

    int x = 8;
    printf("lsh=%d rsh=%d\n", x << 2, x >> 2);

    int neg = -8;
    printf("neg_rsh=%d\n", neg >> 1);

    printf("mask=%u\n", (1u << 5) - 1u);
    return 0;
}
