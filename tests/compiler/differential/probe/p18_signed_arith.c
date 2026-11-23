#include <stdio.h>

int main(void) {
    char c = 200;
    signed char sc = 200;
    printf("char=%d sc=%d\n", c, sc);

    printf("d1=%d m1=%d\n", -7 / 3, -7 % 3);
    printf("d2=%d m2=%d\n", 7 / -3, 7 % -3);
    printf("d3=%d m3=%d\n", -7 / -3, -7 % -3);

    unsigned char uc = 200;
    printf("ucsq=%d\n", uc * uc);
    return 0;
}
