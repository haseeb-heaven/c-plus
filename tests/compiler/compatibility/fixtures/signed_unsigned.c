/* Signed/unsigned conversions and the usual arithmetic conversions. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

int main(void) {
    int a = -1;
    unsigned int u = (unsigned int)a;

    printf("%d\n", (int)(u > 2147483647u));          /* -1 -> large unsigned */
    printf("%d %d\n", (int)(-1 < 1u),                /* 0: -1 becomes unsigned */
                      (int)((unsigned)-1 > 1u));    /* 1 */
    printf("%u\n", (unsigned)(u + 2u));              /* wraps to 1 */
    printf("%d\n", (int)((short)-1 > (unsigned short)1));  /* 0: both -> int */
    printf("%d\n", (int)((long)-1 > (unsigned long)1));    /* 1: long -> ulong */
    return 0;
}
