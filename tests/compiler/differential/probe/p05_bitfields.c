#include <stdio.h>

struct B {
    unsigned a : 3;
    unsigned b : 5;
    unsigned c : 8;
    unsigned d : 16;
};

int main(void) {
    struct B x;
    x.a = 5;
    x.b = 17;
    x.c = 200;
    x.d = 40000;
    printf("sizeof=%zu\n", sizeof(struct B));
    printf("a=%u b=%u c=%u d=%u\n", x.a, x.b, x.c, x.d);
    x.a = 9; /* truncated to 3 bits -> 1 */
    printf("a2=%u\n", x.a);
    return 0;
}
