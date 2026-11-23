/* Integer promotions: char/short operands promote to int in expressions. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

int main(void) {
    signed char sc = -1;
    unsigned char uc = 200;
    short ss = -2;
    unsigned short us = 65535;

    printf("%d %d %d %d\n", (int)sc, (int)uc, (int)ss, (int)us);
    printf("%d %d %d\n",
           (int)(sizeof(sc + 0) == sizeof(int)),
           (int)(sizeof(ss + 0) == sizeof(int)),
           (int)(uc * uc));            /* 40000, not a char wrap */
    printf("%d\n", (int)(~uc));        /* ~200 == -201 as int */
    return 0;
}
