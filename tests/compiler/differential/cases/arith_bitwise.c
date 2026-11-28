#include <stdio.h>
int main(void) {
    unsigned a = 53944u, b = 6986u;
    printf("and=%u or=%u xor=%u\n", a & b, a | b, a ^ b);
    printf("not=%u shl=%u shr=%u\n", ~a, a << 2, b >> 2);
    return 0;
}
