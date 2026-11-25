#include <stdio.h>

union U {
    unsigned u;
    int i;
    unsigned char b[4];
};

int main(void) {
    union U x;
    x.u = 0x01020304u;
    printf("i=%d\n", x.i);
    printf("b=%u %u %u %u\n", x.b[0], x.b[1], x.b[2], x.b[3]);
    return 0;
}
