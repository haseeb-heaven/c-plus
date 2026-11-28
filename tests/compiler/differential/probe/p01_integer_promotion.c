#include <stdio.h>

int main(void) {
    signed char sc = -1;
    unsigned char uc = 255;
    short s = -2;
    unsigned short us = 65535;

    printf("sc=%d sc+1=%d\n", sc, sc + 1);
    printf("uc=%u uc+1=%d\n", (unsigned)uc, uc + 1);
    printf("s=%d s+1=%d\n", s, s + 1);
    printf("us=%u us+1=%d\n", (unsigned)us, us + 1);

    signed char a = -1;
    unsigned int b = 1;
    /* -1 promotes to int, then converts to unsigned for the comparison */
    printf("neg_lt_unsigned=%d\n", a < b);
    return 0;
}
