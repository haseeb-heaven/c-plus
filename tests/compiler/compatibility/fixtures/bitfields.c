/* Bitfield declaration, storage and truncation on assignment. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

struct B {
    unsigned int a : 3;
    unsigned int   : 2;          /* unnamed bitfield */
    unsigned int b : 4;
    unsigned int c : 1;
};

int main(void) {
    struct B x;
    x.a = 5;
    x.b = 10;
    x.c = 1;
    printf("%u %u %u\n", x.a, x.b, x.c);
    printf("%d\n", (int)sizeof(struct B));
    x.a = 9;                     /* truncated to 3 bits: 9 & 7 == 1 */
    printf("%u\n", x.a);
    return 0;
}
