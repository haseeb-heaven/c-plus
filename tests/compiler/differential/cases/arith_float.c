#include <stdio.h>
int main(void) {
    double a = 8187 / 8.0;
    double b = 2628 / 16.0;
    printf("add=%.6f sub=%.6f\n", a + b, a - b);
    printf("mul=%.6f div=%.6f\n", a * b, a / b);
    return 0;
}
